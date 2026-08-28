#include "../Launch/EditorRunResourceRouting.h"
#include "../Launch/EditorRunRuntimeSession.h"
#include "../File/RootedResourceReader.h"
#include "../Resource/ResourceCatalog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
namespace fs = std::filesystem;

bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << "\n";
	}
	return condition;
}

bool hasCatalogDiagnostic(
	const RuntimeResource::ExactSelectionResult& result,
	const std::string& code)
{
	return std::any_of(
		result.diagnostics.begin(),
		result.diagnostics.end(),
		[&code](const RuntimeResource::CatalogDiagnostic& diagnostic)
		{
			return diagnostic.code == code;
		});
}

fs::path makeUniqueTestRoot()
{
	const auto ticks = std::chrono::high_resolution_clock::now()
		.time_since_epoch().count();
	return fs::temp_directory_path() /
		fs::u8path(
			u8"jxqy 编辑器资源预检 " +
			std::to_string(static_cast<long long>(ticks)));
}

bool writeText(const fs::path& path, const std::string& content)
{
	std::error_code error;
	fs::create_directories(path.parent_path(), error);
	if (error)
	{
		return false;
	}
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(
		content.data(), static_cast<std::streamsize>(content.size()));
	return output.good();
}

std::vector<std::uint8_t> readBytes(const fs::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return std::vector<std::uint8_t>(
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes)
{
	std::uint64_t value = 1469598103934665603ULL;
	for (std::uint8_t byte : bytes)
	{
		value ^= byte;
		value *= 1099511628211ULL;
	}
	return value;
}

struct FileSnapshot
{
	std::vector<std::uint8_t> bytes;
	std::uint64_t hash = 0;
	std::uintmax_t size = 0;
	fs::file_time_type modified;
};

FileSnapshot snapshotFile(const fs::path& path)
{
	FileSnapshot snapshot;
	std::error_code error;
	snapshot.bytes = readBytes(path);
	snapshot.hash = fnv1a(snapshot.bytes);
	snapshot.size = fs::file_size(path, error);
	snapshot.modified = fs::last_write_time(path, error);
	return snapshot;
}

bool sameSnapshot(const fs::path& path, const FileSnapshot& expected)
{
	std::error_code error;
	const std::vector<std::uint8_t> bytes = readBytes(path);
	return bytes == expected.bytes &&
		fnv1a(bytes) == expected.hash &&
		fs::file_size(path, error) == expected.size &&
		!error &&
		fs::last_write_time(path, error) == expected.modified &&
		!error;
}

EditorRun::Descriptor makeDescriptor(
	const fs::path& collectionRoot,
	const std::string& resourcePackId,
	const std::string& mapPath)
{
	EditorRun::Descriptor descriptor;
	descriptor.sessionId =
		"01234567-89ab-cdef-8123-456789abcdef";
	descriptor.assetsCollectionRoot = collectionRoot;
	descriptor.activeResourcePackId = resourcePackId;
	descriptor.target.sceneId = "scene";
	descriptor.target.sceneName = "Scene";
	descriptor.target.mapPath = mapPath;
	const fs::path session =
		collectionRoot.parent_path() / "session";
	descriptor.overlayRoot = session / "overlay";
	descriptor.isolatedSaveRoot = session / "save";
	descriptor.applicationStateRoot =
		session / "application-state";
	descriptor.diagnosticsPath =
		session / "diagnostics" / "events.jsonl";
	descriptor.logPath =
		session / "diagnostics" / "game.log";
	return descriptor;
}

std::string folderName(const fs::path& path)
{
	return path.filename().u8string();
}

bool createFileSymlink(
	const fs::path& target, const fs::path& link)
{
#ifdef _WIN32
	const DWORD flags =
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
		SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#else
		0;
#endif
	return CreateSymbolicLinkW(
		link.c_str(), target.c_str(), flags) != FALSE;
#else
	std::error_code error;
	fs::create_symlink(target, link, error);
	return !error;
#endif
}

bool createDirectorySymlink(
	const fs::path& target, const fs::path& link)
{
#ifdef _WIN32
	const DWORD flags =
		SYMBOLIC_LINK_FLAG_DIRECTORY |
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
		SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#else
		0;
#endif
	return CreateSymbolicLinkW(
		link.c_str(), target.c_str(), flags) != FALSE;
#else
	std::error_code error;
	fs::create_directory_symlink(target, link, error);
	return !error;
#endif
}

bool createHardLink(
	const fs::path& target, const fs::path& link)
{
	std::error_code error;
	fs::create_hard_link(target, link, error);
	return !error;
}

bool testProfileMarkedJxqy2(const fs::path& root)
{
	const fs::path loose = root / fs::u8path(u8"松散 资源");
	const fs::path looseMap = loose / fs::u8path(u8"map/中都.map");
	bool ok = writeText(looseMap, "map");

	EditorRun::Descriptor descriptor =
		makeDescriptor(loose, "jxqy2", u8"map/中都.map");
	const EditorRun::ResourcePreparationResult unconverted =
		EditorRun::prepareEditorRunResources(
			descriptor, "1.4.3");
	ok = check(
		unconverted.succeeded() &&
			unconverted.prepared.resources.
				orderedContentRoots.empty() &&
			unconverted.diagnosticCode ==
				"editor_run.resource.id_not_found",
		"a directory without game_profile.ini remains an empty formal route even when legacy resource files exist") &&
		ok;

	ok = check(
		writeText(
			loose / "game_profile.ini",
			"[Game]\nId=JXQY2\nName=JXQY2\nType=0\n"),
		"write the converted-pack profile marker") && ok;
	const EditorRun::ResourcePreparationResult prepared =
		EditorRun::prepareEditorRunResources(
			descriptor, "1.4.3");
	if (!prepared.succeeded())
	{
		std::cerr << "loose preflight detail: code="
			<< prepared.diagnosticCode
			<< " message=" << prepared.message << "\n";
	}
	ok = check(
		prepared.succeeded() &&
			prepared.prepared.resources.
				canonicalActiveResourcePackId == "JXQY2" &&
			prepared.prepared.resources.
				orderedContentRoots.size() == 1 &&
			prepared.prepared.target.map.searchRootIndex ==
				EditorRun::SearchAllResourceRoots,
		"a profile-marked root materializes canonical JXQY2 and defers target lookup") &&
		ok;

	EditorRun::RuntimeSession looseSession;
	looseSession.descriptor = descriptor;
	const fs::path looseSessionRoot =
		root / "loose-trace-session";
	looseSession.descriptor.overlayRoot =
		looseSessionRoot / "overlay";
	looseSession.descriptor.isolatedSaveRoot =
		looseSessionRoot / "save";
	looseSession.descriptor.applicationStateRoot =
		looseSessionRoot / "application-state";
	looseSession.descriptor.diagnosticsPath =
		looseSessionRoot / "diagnostics/events.jsonl";
	looseSession.descriptor.logPath =
		looseSessionRoot / "diagnostics/game.log";
	const std::array<fs::path, 4> looseOutputRoots =
		{
			looseSession.descriptor.overlayRoot,
			looseSession.descriptor.isolatedSaveRoot,
			looseSession.descriptor.applicationStateRoot,
			looseSession.descriptor.
				diagnosticsPath.parent_path()
		};
	ok = check(
		writeText(
			looseSession.descriptor.overlayRoot /
				"script/dirty.lua",
			"return 'dirty loose overlay'\n"),
		"write loose active-root overlay origin") &&
		ok;
	for (std::size_t index = 0;
		index < looseOutputRoots.size();
		++index)
	{
		std::error_code createError;
		fs::create_directories(
			looseOutputRoots[index],
			createError);
		const RootedResourceReader::RootAnchorResult
			opened =
				RootedResourceReader::openRootAnchor(
					looseOutputRoots[index]);
		ok = check(
			!createError && opened.succeeded(),
			"open loose trace private output identity") &&
			ok;
		if (opened.succeeded())
		{
			looseSession.outputDirectoryIdentities[index] =
				opened.anchor.identity();
		}
	}
	EditorRun::RuntimeSessionOverlayOrigin looseOrigin;
	looseOrigin.virtualPath = "script/dirty.lua";
	looseOrigin.rootOrdinal = 255;
	looseOrigin.rootKind =
		EditorRun::RuntimeTraceRootKind::Active;
	looseOrigin.resourcePackId =
		prepared.prepared.resources.
			orderedContentRoots.front().
				resourcePackId;
	looseOrigin.rootPath =
		prepared.prepared.resources.
			orderedContentRoots.front().root;
	looseSession.traceOverlayOrigins.push_back(
		std::move(looseOrigin));
	const EditorRun::ResourcePreparationResult looseTrace =
		EditorRun::prepareEditorRunResources(
			looseSession,
			"1.4.3");
	const bool looseTraceMapped =
		looseTrace.succeeded() &&
		!looseTrace.prepared.orderedSearchRoots.empty() &&
		looseTrace.prepared.orderedSearchRoots.front().
			traceOverlayOrigins.size() == 1 &&
		looseTrace.prepared.orderedSearchRoots.front().
			traceOverlayOrigins.front().contentRoot.kind ==
				EditorRun::RuntimeTraceRootKind::Active &&
		looseTrace.prepared.orderedSearchRoots.front().
			traceOverlayOrigins.front().
				contentRoot.ordinal == 0 &&
		looseTrace.prepared.orderedSearchRoots.front().
			traceOverlayOrigins.front().
				contentRoot.resourcePackId ==
					std::optional<std::string>(
						"JXQY2");
	ok = check(
		looseTraceMapped,
		"a profile-backed active root remaps through the same JXQY2 logical identity written by the editor") &&
		ok;

	descriptor.activeResourcePackId = "OTHER";
	const EditorRun::ResourcePreparationResult missing =
		EditorRun::prepareEditorRunResources(
			descriptor, "1.4.3");
	ok = check(
		missing.succeeded() &&
			missing.prepared.resources.orderedContentRoots.empty() &&
			missing.prepared.target.map.searchRootIndex ==
				EditorRun::SearchAllResourceRoots &&
			missing.diagnosticCode ==
				"editor_run.resource.id_not_found",
		"a missing requested pack becomes an empty formal route without blocking editor-run") && ok;

	ok = writeText(
		loose / "resources.ini",
		"[Pack.BROKEN]\nEnabled=1\n") && ok;
	const EditorRun::ResourcePreparationResult brokenIndex =
		EditorRun::prepareEditorRunResources(
			makeDescriptor(loose, "JXQY2", u8"map/中都.map"),
			"1.4.3");
	ok = check(
		brokenIndex.succeeded() &&
			brokenIndex.prepared.resources.
				canonicalActiveResourcePackId == "JXQY2" &&
			brokenIndex.prepared.resources.
				orderedContentRoots.size() == 1,
		"an invalid optional index does not hide the independently profile-marked root") &&
		ok;
	return ok;
}

bool createOrderedCollectionFixture(
	const fs::path& collection)
{
	bool created = writeText(
			collection / "resources.ini",
			"[Collection]\n"
			"CommonPath=common\n"
			"\n"
			"[Pack.MOD]\n"
			"Id=MOD\n"
			"Path=mod\n"
			"Manifest=game_profile.ini\n"
			"\n"
			"[Pack.MID]\n"
			"Id=MID\n"
			"Path=mid\n"
			"Manifest=game_profile.ini\n"
			"\n"
			"[Pack.BASE]\n"
			"Id=BASE\n"
			"Path=base\n"
			"Manifest=game_profile.ini\n"
			"\n"
			"[Pack.BASE2]\n"
			"Id=BASE2\n"
			"Path=base2\n"
			"Manifest=game_profile.ini\n") &&
		writeText(
			collection / "mod/game_profile.ini",
			"[Game]\n"
			"Id=MANIFEST_MOD\n"
			"Name=Mod\n"
			"[Resource]\n"
			"DependencyId=MID,BASE2,PATHBASE\n"
			"[Release]\n"
			"MinimumEngineVersion=1.4.0\n") &&
		writeText(
			collection / "mid/game_profile.ini",
			"[Game]\nId=MID\nName=Middle\n"
			"[Resource]\nDependencyId=BASE\n") &&
		writeText(
			collection / "base/game_profile.ini",
			"[Game]\nId=BASE\nName=Base\nType=0\n") &&
		writeText(
			collection / "base2/game_profile.ini",
			"[Game]\nId=BASE2\nName=Base 2\nType=1\n") &&
		writeText(
			collection / "path-base/game_profile.ini",
			"[Game]\nId=PATHBASE\nName=Path Base\nType=2\n") &&
		writeText(
			collection / "mod/map/scene.map", "map") &&
		writeText(
			collection / "mod/script/common/upper.lua",
			"return 'active'\n") &&
		writeText(
			collection / "mid/script/common/upper.lua",
			"return 'dependency'\n") &&
		writeText(
			collection /
				"mod/script/common/dependency.lua",
			"return 'active dependency-name duplicate'\n") &&
		writeText(
			collection /
				"base/script/common/dependency.lua",
			"return 'base dependency-name duplicate'\n") &&
		writeText(
			collection / "common/config/common.txt", "common") &&
		writeText(
			collection / "save/system/resource_selection.ini",
			"[ResourceSelection]\nId=BASE2\n");
	return created;
}

bool testExactSelectionAndOrder(const fs::path& root)
{
	const fs::path collection = root / "ordered";
	bool ok = check(
		createOrderedCollectionFixture(collection),
		"create ordered catalog fixture");
	const fs::path recent =
		collection / "save/system/resource_selection.ini";
	const FileSnapshot recentBefore = snapshotFile(recent);

	const RuntimeResource::ExactSelectionResult selection =
		RuntimeResource::resolveExactResourceSelection(
			collection, "mAnIfEsT_mOd");
	ok = check(
		selection.succeeded() &&
			selection.selection.canonicalActiveResourcePackId ==
				"MANIFEST_MOD",
		"exact selection is ASCII case-insensitive and reports the game_profile.ini canonical ID") &&
		ok;
	for (const std::string& paddedId :
		{
			std::string(" MANIFEST_MOD"),
			std::string("MANIFEST_MOD "),
			std::string(" MANIFEST_MOD ")
		})
	{
		const RuntimeResource::ExactSelectionResult padded =
			RuntimeResource::resolveExactResourceSelection(
				collection, paddedId);
		ok = check(
			padded.error ==
				RuntimeResource::ExactSelectionError::
					ActiveResourcePackIdNotFound &&
				padded.resourcePackId == paddedId,
			"requested resource ID whitespace participates in exact matching") &&
			ok;
	}
	const auto& roots = selection.selection.orderedContentRoots;
	ok = check(
		roots.size() == 6 &&
			roots[0].kind ==
				RuntimeResource::ContentRootKind::Active &&
			folderName(roots[0].root) == "mod" &&
			folderName(roots[1].root) == "mid" &&
			folderName(roots[2].root) == "base" &&
			folderName(roots[3].root) == "base2" &&
			roots[4].kind ==
				RuntimeResource::ContentRootKind::DependencyId &&
			folderName(roots[4].root) == "path-base" &&
			roots[5].kind ==
				RuntimeResource::ContentRootKind::Common &&
			folderName(roots[5].root) == "common",
		"content roots preserve active, left-to-right DFS dependencies, and Common order") &&
		ok;
	ok = check(
		sameSnapshot(recent, recentBefore),
		"exact selection neither reads for promotion nor modifies recent selection") &&
		ok;

	const EditorRun::ResourcePreparationResult prepared =
		EditorRun::prepareEditorRunResources(
			makeDescriptor(
				collection, "MANIFEST_MOD", "map/scene.map"),
			"1.4.3");
	ok = check(
		prepared.succeeded() &&
			prepared.prepared.resources.orderedContentRoots.size() ==
				roots.size() &&
			prepared.prepared.target.map.searchRootIndex ==
				EditorRun::SearchAllResourceRoots,
		"editor-run uses the shared dependency routing and leaves target lookup to the runtime loader") &&
		ok;
	ok = check(
		sameSnapshot(recent, recentBefore),
		"pure resource routing preserves recent selection bytes, hash, size, and mtime") &&
		ok;
	return ok;
}

bool testConfiguredHostPathSemantics(const fs::path& root)
{
	const fs::path collection = root / "configured-paths";
	bool ok = writeText(
		collection / "resources.ini",
		"[Collection]\n"
		"CommonPath=shared\\common\n") &&
		writeText(
			collection / "mod/game_profile.ini",
			"[Game]\n"
			"Id=MOD\n"
			"Name=Mod\n"
			"[Resource]\n"
			"DependencyId=BASE\n") &&
		writeText(
			collection / "base/game_profile.ini",
			"[Game]\n"
			"Id=BASE\n"
			"Name=Base\n"
			"Type=0\n") &&
		writeText(
			collection / "shared/common/map/path.map",
			"path");
	const EditorRun::ResourcePreparationResult relativeIndex =
		EditorRun::prepareEditorRunResources(
			makeDescriptor(
				collection, "MOD", "map/path.map"),
			"1.4.3");
	ok = check(
		relativeIndex.succeeded() &&
			relativeIndex.prepared.resources.
				orderedContentRoots.size() == 3 &&
			folderName(
				relativeIndex.prepared.resources.
					orderedContentRoots[0].root) ==
				"mod" &&
			folderName(
				relativeIndex.prepared.resources.
					orderedContentRoots[1].root) ==
				"base" &&
			relativeIndex.prepared.resources.
				orderedContentRoots[2].root ==
				fs::canonical(
					collection / "shared/common") &&
			relativeIndex.prepared.target.map.searchRootIndex ==
				EditorRun::SearchAllResourceRoots,
		"collection CommonPath keeps portable backslash semantics without Pack index entries") &&
		ok;
	return ok;
}

bool testIdAndDependencyFailures(const fs::path& root)
{
	bool ok = true;
	const fs::path duplicate = root / "duplicate";
	ok = writeText(
		duplicate / "resources.ini",
		"[Pack.A]\nPath=a\n"
		"[Pack.B]\nPath=b\n"
		"[Pack.MOD]\nPath=mod\n") && ok;
	ok = writeText(
		duplicate / "a/game_profile.ini",
		"[Game]\nId=Dupe\nName=A\n") &&
		writeText(
			duplicate / "a/map/owner.map",
			"first-owner") && ok;
	ok = writeText(
		duplicate / "b/game_profile.ini",
		"[Game]\nId=dUPE\nName=B\n") &&
		writeText(
			duplicate / "b/map/owner.map",
			"second-owner") && ok;
	ok = writeText(
		duplicate / "mod/game_profile.ini",
		"[Game]\nId=MOD\nName=Mod\n"
		"[Resource]\nDependencyId=DUPE\n") && ok;
	ok = writeText(
		duplicate / "ui-mod/game_profile.ini",
		"[Game]\nId=UIMOD\nName=UI Mod\n"
		"[UI]\nBaseId=DUPE\n") && ok;
	const RuntimeResource::ExactSelectionResult ambiguous =
		RuntimeResource::resolveExactResourceSelection(
			duplicate, "DUPE");
	ok = check(
		!ambiguous.succeeded() &&
			ambiguous.error == RuntimeResource::ExactSelectionError::
				ActiveResourcePackIdAmbiguous &&
			ambiguous.diagnosticCode ==
				"resource.catalog.active_id_ambiguous",
		"duplicate Game.Id lookup rejects every ambiguous owner") &&
		ok;
	const RuntimeResource::ExactSelectionResult exactDuplicate =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			duplicate, "pack.b");
	ok = check(
		!exactDuplicate.succeeded() &&
			exactDuplicate.error == RuntimeResource::ExactSelectionError::
				ActiveResourcePackIdAmbiguous,
		"stable entry keys cannot bypass duplicate Game.Id rejection") &&
		ok;
	const RuntimeResource::ExactSelectionResult ambiguousDependency =
		RuntimeResource::resolveExactResourceSelection(
			duplicate, "MOD");
	ok = check(
		ambiguousDependency.succeeded() &&
			ambiguousDependency.selection.orderedContentRoots.size() == 1 &&
			hasCatalogDiagnostic(
				ambiguousDependency,
				"resource.catalog.dependency_id_ambiguous"),
		"an ambiguous dependency ID is skipped without hiding local content") &&
		ok;
	const RuntimeResource::ExactSelectionResult ambiguousUiDependency =
		RuntimeResource::resolveExactResourceSelection(
			duplicate, "UIMOD");
	ok = check(
		!ambiguousUiDependency.succeeded() &&
			ambiguousUiDependency.error ==
				RuntimeResource::ExactSelectionError::
					UiDependencyAmbiguous &&
			ambiguousUiDependency.diagnosticCode ==
				"resource.catalog.ui_dependency_id_ambiguous",
		"an ambiguous UI dependency ID rejects the selection") &&
		ok;

	const fs::path missing = root / "missing-dependency";
	ok = writeText(
		missing / "resources.ini",
		"[Pack.MOD]\nId=MOD\nPath=mod\n") && ok;
	ok = writeText(
		missing / "mod/game_profile.ini",
		"[Game]\nId=MOD\nName=Mod\n"
		"[Resource]\nDependencyId=NOPE\n") && ok;
	const RuntimeResource::ExactSelectionResult missingDependency =
		RuntimeResource::resolveExactResourceSelection(
			missing, "MOD");
	ok = check(
		missingDependency.succeeded() &&
			missingDependency.selection.
				orderedContentRoots.size() == 1 &&
			hasCatalogDiagnostic(
				missingDependency,
				"resource.catalog.dependency_not_found"),
		"missing DependencyId leaves local content available and reports a capability warning") &&
		ok;

	const fs::path cycle = root / "dependency-cycle";
	ok = writeText(
		cycle / "resources.ini",
		"[Pack.A]\nId=A\nPath=a\n"
		"[Pack.B]\nId=B\nPath=b\n") && ok;
	ok = writeText(
		cycle / "a/game_profile.ini",
		"[Game]\nId=A\nName=A\n"
		"[Resource]\nDependencyId=B\n") && ok;
	ok = writeText(
		cycle / "b/game_profile.ini",
		"[Game]\nId=B\nName=B\n"
		"[Resource]\nDependencyId=A\n") && ok;
	const RuntimeResource::ExactSelectionResult cyclic =
		RuntimeResource::resolveExactResourceSelection(cycle, "A");
	ok = check(
		cyclic.succeeded() &&
			cyclic.selection.orderedContentRoots.size() == 2 &&
			hasCatalogDiagnostic(
				cyclic,
				"resource.catalog.dependency_cycle_ignored"),
		"dependency cycles retain each reachable local root once and report a warning") &&
		ok;
	return ok;
}

bool testCompatibility(const fs::path& root)
{
	const fs::path collection = root / "compatibility";
	bool ok = writeText(collection / "map/scene.map", "map");
	auto writeProfile =
		[&collection](const std::string& minimum)
		{
			std::string text =
				"[Game]\nId=MOD\nName=Mod\nType=0\n";
			if (!minimum.empty())
			{
				text +=
					"[Release]\nMinimumEngineVersion=" +
					minimum + "\n";
			}
			return writeText(
				collection / "game_profile.ini", text);
		};

	ok = writeProfile("") && ok;
	EditorRun::Descriptor descriptor =
		makeDescriptor(collection, "mod", "map/scene.map");
	ok = check(
		EditorRun::prepareEditorRunResources(
			descriptor, "not-semver").succeeded(),
		"legacy manifests route without parsing current SemVer") &&
		ok;

	ok = writeProfile("9999.0.0") && ok;
	ok = check(
		EditorRun::prepareEditorRunResources(
			descriptor, "1.4.3").succeeded(),
		"newer minimum versions remain advisory") && ok;

	ok = writeProfile("not-semver") && ok;
	ok = check(
		EditorRun::prepareEditorRunResources(
			descriptor, "1.4.3").succeeded(),
		"invalid minimum versions remain advisory") && ok;

	ok = writeProfile("1.0.0") && ok;
	ok = check(
		EditorRun::prepareEditorRunResources(
			descriptor, "not-semver").succeeded(),
		"current engine version text does not participate in resource routing") && ok;
	return ok;
}

bool testDeferredTargetLookup(const fs::path& root)
{
	(void)root;
	EditorRun::SceneTarget target;
	target.mapPath = "map/missing.map";
	target.npcPath = "npc/missing.npc";
	target.objectPath = "obj/missing.obj";
	target.entryScriptPath = "script/missing.lua";
	const EditorRun::ResolvedSceneTarget prepared =
		EditorRun::prepareSceneTarget(target);
	return check(
		prepared.map.virtualPath == target.mapPath &&
			prepared.map.searchRootIndex ==
				EditorRun::SearchAllResourceRoots &&
			prepared.npc.has_value() &&
			prepared.npc->searchRootIndex ==
				EditorRun::SearchAllResourceRoots &&
			prepared.object.has_value() &&
			prepared.object->searchRootIndex ==
				EditorRun::SearchAllResourceRoots &&
			prepared.entryScript.has_value() &&
			prepared.entryScript->searchRootIndex ==
				EditorRun::SearchAllResourceRoots,
		"editor-run target preparation performs no existence probe and defers every resource lookup to runtime");
}

bool testRuntimeResourceRoutingContractHandoff(
	const fs::path& root)
{
	const fs::path collection =
		root / "runtime-resource-routing-contract";
	bool ok = check(
		createOrderedCollectionFixture(collection),
		"create runtime resource-routing contract fixture");
	EditorRun::RuntimeSession session;
	session.descriptor = makeDescriptor(
		collection, "MANIFEST_MOD", "map/scene.map");
	const fs::path isolatedSessionRoot =
		root /
			"runtime-resource-routing-contract-session";
	session.descriptor.overlayRoot =
		isolatedSessionRoot / "overlay";
	session.descriptor.isolatedSaveRoot =
		isolatedSessionRoot / "save";
	session.descriptor.applicationStateRoot =
		isolatedSessionRoot / "application-state";
	session.descriptor.diagnosticsPath =
		isolatedSessionRoot / "diagnostics" /
			"events.jsonl";
	session.descriptor.logPath =
		isolatedSessionRoot / "diagnostics" /
			"game.log";
	const std::array<fs::path, 4> outputRoots =
		{
			session.descriptor.overlayRoot,
			session.descriptor.isolatedSaveRoot,
			session.descriptor.applicationStateRoot,
			session.descriptor.diagnosticsPath.parent_path()
		};
	for (std::size_t index = 0;
		index < outputRoots.size();
		++index)
	{
		std::error_code createError;
		fs::create_directories(
			outputRoots[index], createError);
		if (index == 0)
		{
			ok = check(
				writeText(
					outputRoots[index] /
						"script" / "common" /
						"upper.lua",
					"return 1\n"),
				"write active overlay origin fixture") &&
				ok;
			ok = check(
				writeText(
					outputRoots[index] /
						"script" / "common" /
						"common.lua",
					"return 4\n"),
				"write common-root overlay origin fixture") &&
				ok;
			ok = check(
				writeText(
					outputRoots[index] /
						"script" / "common" /
						"dependency.lua",
					"return 5\n"),
				"write dependency-id overlay origin fixture") &&
				ok;
			ok = check(
				writeText(
					outputRoots[index] /
						"script" / "common" /
						"overlayonly.lua",
					"return 3\n"),
				"write overlay-only origin fixture") &&
				ok;
			ok = check(
				writeText(
					outputRoots[index] /
						"script" / "common" /
						"pathdependency.lua",
					"return 6\n"),
				"write dependency-path overlay origin fixture") &&
				ok;
		}
		const RootedResourceReader::RootAnchorResult
			opened =
				RootedResourceReader::openRootAnchor(
					outputRoots[index]);
		ok = check(
			!createError && opened.succeeded(),
			"open runtime output identity fixture") &&
			ok;
		if (opened.succeeded())
		{
			session.outputDirectoryIdentities[index] =
				opened.anchor.identity();
		}
	}

	EditorRun::ResourcePreparationResult baseline =
		EditorRun::prepareEditorRunResources(
			session.descriptor, "1.4.3");
	ok = check(
		baseline.succeeded(),
		"prepare baseline selection for runtime resource-routing proof") &&
		ok;
	if (!baseline.succeeded())
	{
		return false;
	}
	const auto baselineRoot =
		[&baseline](
			RuntimeResource::ContentRootKind kind,
			std::string_view resourcePackId = {})
			-> const RuntimeResource::ContentRoot*
		{
			const auto found = std::find_if(
				baseline.prepared.resources.
					orderedContentRoots.cbegin(),
				baseline.prepared.resources.
					orderedContentRoots.cend(),
				[kind, resourcePackId](
					const RuntimeResource::ContentRoot&
						candidate)
				{
					return candidate.kind == kind &&
						(resourcePackId.empty() ||
							candidate.resourcePackId ==
								resourcePackId);
				});
			return found ==
					baseline.prepared.resources.
						orderedContentRoots.cend()
				? nullptr
				: &*found;
		};
	const RuntimeResource::ContentRoot* baselineActive =
		baselineRoot(
			RuntimeResource::ContentRootKind::Active);
	const RuntimeResource::ContentRoot* baselineBase =
		baselineRoot(
			RuntimeResource::ContentRootKind::DependencyId,
			"BASE");
	const RuntimeResource::ContentRoot* baselinePath =
		baselineRoot(
			RuntimeResource::ContentRootKind::DependencyId,
			"PATHBASE");
	const RuntimeResource::ContentRoot* baselineCommon =
		baselineRoot(
			RuntimeResource::ContentRootKind::Common);
	if (!baselineActive || !baselineBase ||
		!baselinePath || !baselineCommon)
	{
		return check(
			false,
			"baseline catalog exposes every logical trace root kind");
	}
	const fs::path baselineActivePath =
		baselineActive->root;
	const std::string baselineActiveId =
		baselineActive->resourcePackId;
	const fs::path baselineBasePath =
		baselineBase->root;
	const fs::path baselinePathDependency =
		baselinePath->root;
	const fs::path baselineCommonPath =
		baselineCommon->root;
	baseline = {};
	const auto addOrigin =
		[&session](
			std::string virtualPath,
			std::uint64_t staleOrdinal,
			EditorRun::RuntimeTraceRootKind kind,
			std::optional<std::string> resourcePackId,
			fs::path rootPath)
		{
			EditorRun::RuntimeSessionOverlayOrigin origin;
			origin.virtualPath = std::move(virtualPath);
			origin.rootOrdinal = staleOrdinal;
			origin.rootKind = kind;
			origin.resourcePackId =
				std::move(resourcePackId);
			origin.rootPath = std::move(rootPath);
			session.traceOverlayOrigins.push_back(
				std::move(origin));
		};
	addOrigin(
		"script/common/common.lua",
		250,
		EditorRun::RuntimeTraceRootKind::Common,
		std::nullopt,
		baselineCommonPath);
	addOrigin(
		"script/common/dependency.lua",
		2,
		EditorRun::RuntimeTraceRootKind::DependencyId,
		std::string("BASE"),
		baselineBasePath);
	addOrigin(
		"script/common/overlayonly.lua",
		254,
		EditorRun::RuntimeTraceRootKind::DependencyId,
		std::string("REMOVED"),
		root / "removed-dependency-path");
	addOrigin(
		"script/common/pathdependency.lua",
		4,
		EditorRun::RuntimeTraceRootKind::DependencyId,
		std::string("PATHBASE"),
		baselinePathDependency);
	addOrigin(
		"script/common/upper.lua",
		255,
		EditorRun::RuntimeTraceRootKind::Active,
		baselineActiveId,
		baselineActivePath);

	ok = check(
		writeText(
			collection / "mod/game_profile.ini",
			"[Game]\n"
			"Id=MANIFEST_MOD\n"
			"Name=Mod\n"
			"[Resource]\n"
			"DependencyId=BASE2,MID,PATHBASE\n"
			"[Release]\n"
			"MinimumEngineVersion=1.4.0\n"),
		"reorder the current catalog after overlay provenance was recorded") &&
		ok;
	EditorRun::ResourcePreparationResult accepted =
		EditorRun::prepareEditorRunResources(
			session, "1.4.3");
	const bool formalAnchorsReleased =
		std::all_of(
			accepted.prepared.orderedSearchRoots.cbegin(),
			accepted.prepared.orderedSearchRoots.cend(),
			[](const EditorRun::SearchRoot& root)
			{
				return root.kind ==
						EditorRun::SearchRootKind::Overlay ||
					!root.anchor.valid();
			});
	const auto originByPath =
		[&accepted](std::string_view virtualPath)
			-> std::vector<
				EditorRun::TraceOverlayOrigin>::
					const_iterator
		{
			if (accepted.prepared.
				orderedSearchRoots.empty())
			{
				return {};
			}
			const auto& origins =
				accepted.prepared.
					orderedSearchRoots.front().
						traceOverlayOrigins;
			return std::find_if(
				origins.cbegin(),
				origins.cend(),
				[virtualPath](
					const EditorRun::TraceOverlayOrigin&
						origin)
				{
					return origin.virtualPath ==
						virtualPath;
				});
		};
	const auto activeOrigin =
		accepted.prepared.orderedSearchRoots.empty()
			? std::vector<EditorRun::TraceOverlayOrigin>::
				const_iterator()
			: originByPath("script/common/upper.lua");
	const auto dependencyOrigin =
		accepted.prepared.orderedSearchRoots.empty()
			? std::vector<EditorRun::TraceOverlayOrigin>::
				const_iterator()
			: originByPath(
				"script/common/dependency.lua");
	const auto pathOrigin =
		accepted.prepared.orderedSearchRoots.empty()
			? std::vector<EditorRun::TraceOverlayOrigin>::
				const_iterator()
			: originByPath(
				"script/common/pathdependency.lua");
	const auto commonOrigin =
		accepted.prepared.orderedSearchRoots.empty()
			? std::vector<EditorRun::TraceOverlayOrigin>::
				const_iterator()
			: originByPath(
				"script/common/common.lua");
	const auto unknownOrigin =
		accepted.prepared.orderedSearchRoots.empty()
			? std::vector<EditorRun::TraceOverlayOrigin>::
				const_iterator()
			: originByPath(
				"script/common/overlayonly.lua");
	std::optional<std::uint64_t> currentBaseOrdinal;
	std::optional<std::uint64_t>
		currentPathDependencyOrdinal;
	std::optional<std::uint64_t> currentCommonOrdinal;
	for (std::size_t ordinal = 0;
		ordinal < accepted.prepared.resources.
			orderedContentRoots.size();
		++ordinal)
	{
		const RuntimeResource::ContentRoot& candidate =
			accepted.prepared.resources.
				orderedContentRoots[ordinal];
		if (candidate.kind ==
				RuntimeResource::ContentRootKind::
					DependencyId &&
			candidate.resourcePackId == "BASE")
		{
			currentBaseOrdinal = ordinal;
		}
		else if (candidate.kind ==
				RuntimeResource::ContentRootKind::DependencyId &&
			candidate.resourcePackId == "PATHBASE")
		{
			currentPathDependencyOrdinal = ordinal;
		}
		else if (candidate.kind ==
			RuntimeResource::ContentRootKind::Common)
		{
			currentCommonOrdinal = ordinal;
		}
	}
	const auto originEnd =
		accepted.prepared.orderedSearchRoots.empty()
			? std::vector<EditorRun::TraceOverlayOrigin>::
				const_iterator()
			: accepted.prepared.
				orderedSearchRoots.front().
					traceOverlayOrigins.cend();
	ok = check(
		accepted.succeeded() &&
			formalAnchorsReleased &&
			!accepted.prepared.orderedSearchRoots.empty() &&
			accepted.prepared.orderedSearchRoots.front().
				traceOverlayOrigins.size() == 5 &&
			activeOrigin != originEnd &&
			activeOrigin->contentRoot.kind ==
				EditorRun::RuntimeTraceRootKind::Active &&
			activeOrigin->contentRoot.ordinal == 0 &&
			activeOrigin->contentRoot.resourcePackId ==
				baselineActiveId &&
			currentBaseOrdinal &&
			*currentBaseOrdinal != 2 &&
			dependencyOrigin != originEnd &&
			dependencyOrigin->contentRoot.kind ==
				EditorRun::RuntimeTraceRootKind::
					DependencyId &&
			dependencyOrigin->contentRoot.ordinal ==
				*currentBaseOrdinal &&
			dependencyOrigin->contentRoot.resourcePackId ==
				std::optional<std::string>("BASE") &&
			currentPathDependencyOrdinal &&
			pathOrigin != originEnd &&
			pathOrigin->contentRoot.ordinal ==
				*currentPathDependencyOrdinal &&
			pathOrigin->contentRoot.kind ==
				EditorRun::RuntimeTraceRootKind::
					DependencyId &&
			currentCommonOrdinal &&
			commonOrigin != originEnd &&
			commonOrigin->contentRoot.ordinal ==
				*currentCommonOrdinal &&
			commonOrigin->contentRoot.kind ==
				EditorRun::RuntimeTraceRootKind::Common &&
			unknownOrigin != originEnd &&
			unknownOrigin->contentRoot.ordinal ==
				EditorRun::
					UnknownTraceContentRootOrdinal,
		"runtime routing preparation uniquely remaps stable logical origins after catalog reorder and preserves unmatched origins as unknown") &&
		ok;
	accepted = {};

	const fs::path activeRoot = collection / "mod";
	const fs::path originalActiveRoot =
		root / "manifest-mod-generation-a";
	const fs::path replacementActiveRoot =
		root / "manifest-mod-generation-b";
	std::error_code renameError;
	fs::rename(
		activeRoot, originalActiveRoot,
		renameError);
	std::error_code copyError;
	if (!renameError)
	{
		fs::copy(
			originalActiveRoot,
			replacementActiveRoot,
			fs::copy_options::recursive,
			copyError);
		if (!copyError)
		{
			ok = check(
				writeText(
					replacementActiveRoot /
						"game_profile.ini",
					"[Game]\n"
					"Id=MANIFEST_MOD\n"
					"Name=Replacement Mod\n"
					"[Resource]\n"
					"DependencyId=MID,BASE2,PATHBASE\n"
					"[Release]\n"
					"MinimumEngineVersion=1.4.0\n"),
				"write replacement active catalog generation") &&
				ok;
		}
	}
	const bool linkCreated =
		!renameError && !copyError &&
		createDirectorySymlink(
			replacementActiveRoot,
			activeRoot);
	ok = check(
		linkCreated,
		"replace contracted active root A with a formal symlink or junction to valid generation B") &&
		ok;

	EditorRun::ResourcePreparationResult replaced =
		EditorRun::prepareEditorRunResources(
			session, "1.4.3");
	const bool replacementAccepted =
		replaced.succeeded() &&
		replaced.prepared.resources.
			activeManifest.name == "Replacement Mod";
	replaced = {};
	ok = check(
		replacementAccepted,
		"runtime catalog follows a path-rebound formal content root and resolves its current target") &&
		ok;

	std::error_code removeError;
	fs::remove(activeRoot, removeError);
	std::error_code removeReplacementError;
	fs::remove_all(
		replacementActiveRoot,
		removeReplacementError);
	std::error_code restoreError;
	fs::rename(
		originalActiveRoot,
		activeRoot,
		restoreError);
	ok = check(
		!removeError && !removeReplacementError &&
			!restoreError,
		"restore contracted active root generation A") &&
		ok;
	return ok;
}

bool testFormalCollectionRootRedirect(
	const fs::path& root)
{
	const fs::path generationA =
		root / "formal-collection-generation-a";
	const fs::path generationB =
		root / "formal-collection-generation-b";
	const fs::path currentCollection =
		root / "formal-collection-current";
	bool ok = check(
		createOrderedCollectionFixture(generationA) &&
			createOrderedCollectionFixture(generationB) &&
			writeText(
				generationB / "mod/game_profile.ini",
				"[Game]\n"
				"Id=MANIFEST_MOD\n"
				"Name=Redirected Collection\n"
				"[Resource]\n"
				"DependencyId=MID,BASE2,PATHBASE\n"
				"[Release]\n"
				"MinimumEngineVersion=1.4.0\n") &&
			writeText(
				generationB / "mod/map/scene.map",
				"generation-b-map"),
		"create both formal collection generations");
	const bool firstLinkCreated =
		createDirectorySymlink(
			generationA,
			currentCollection);
	ok = check(
		firstLinkCreated,
		"create the formal assets collection link") &&
		ok;

	const EditorRun::Descriptor descriptor =
		makeDescriptor(
			currentCollection,
			"MANIFEST_MOD",
			"map/scene.map");
	const EditorRun::ResourcePreparationResult generationAResult =
		EditorRun::prepareEditorRunResources(
			descriptor,
			"1.4.3");
	const fs::path logicalCollection =
		fs::absolute(currentCollection).lexically_normal();
	const fs::path logicalActive =
		(logicalCollection / "mod").lexically_normal();
	ok = check(
		generationAResult.succeeded() &&
			generationAResult.prepared.resources.
				activeManifest.name == "Mod" &&
			generationAResult.prepared.resources.
				assetsCollectionRoot ==
					logicalCollection &&
			generationAResult.prepared.resources.
				activeResourceRoot ==
					logicalActive &&
			!generationAResult.prepared.
				orderedSearchRoots.empty() &&
			generationAResult.prepared.
				orderedSearchRoots.front().root ==
					logicalActive &&
			!generationAResult.prepared.
				orderedSearchRoots.front().anchor.valid(),
		"runtime selection retains the absolute logical collection and pack alias instead of exporting generation A's canonical target") &&
		ok;

	std::error_code removeError;
	fs::remove(currentCollection, removeError);
	const bool secondLinkCreated =
		!removeError &&
		createDirectorySymlink(
			generationB,
			currentCollection);
	ok = check(
		secondLinkCreated,
		"repoint the formal assets collection link to generation B") &&
		ok;
	const EditorRun::ResolvedSceneTarget retainedRoute =
		EditorRun::prepareSceneTarget(descriptor.target);
	const RootedResourceReader::Result retainedMapRead =
		generationAResult.prepared.
			orderedSearchRoots.empty()
			? RootedResourceReader::Result{}
			: RootedResourceReader::
				readBoundedFileFromRoot(
					generationAResult.prepared.
						orderedSearchRoots.front().root,
					"map/scene.map",
					1024);
	const std::string retainedMapText(
		retainedMapRead.bytes.begin(),
		retainedMapRead.bytes.end());
	ok = check(
		retainedRoute.map.searchRootIndex ==
			EditorRun::SearchAllResourceRoots &&
			retainedMapRead.succeeded() &&
			retainedMapText == "generation-b-map",
		"one prepared logical route follows the collection and pack link from A to B without another catalog pass") &&
		ok;
	const EditorRun::ResourcePreparationResult generationBResult =
		EditorRun::prepareEditorRunResources(
			descriptor,
			"1.4.3");
	ok = check(
		generationBResult.succeeded() &&
			generationBResult.prepared.resources.
				activeManifest.name ==
					"Redirected Collection",
		"the next routing preparation resolves the formal collection path's latest target") &&
		ok;

	const fs::path privateOverlayTarget =
		root / "private-overlay-target";
	const fs::path privateOverlayLink =
		root / "private-overlay-link";
	std::error_code createError;
	fs::create_directories(privateOverlayTarget, createError);
	EditorRun::Descriptor linkedOverlayDescriptor =
		descriptor;
	linkedOverlayDescriptor.overlayRoot =
		privateOverlayLink;
	const bool privateOverlayLinkCreated =
		!createError &&
		createDirectorySymlink(
			privateOverlayTarget,
			privateOverlayLink);
	ok = check(
		privateOverlayLinkCreated,
		"create required private overlay link fixture") &&
		ok;
	if (privateOverlayLinkCreated)
	{
		const EditorRun::ResourcePreparationResult linkedOverlay =
			EditorRun::prepareEditorRunResources(
				linkedOverlayDescriptor,
				"1.4.3");
		ok = check(
			linkedOverlay.error ==
				EditorRun::ResourcePreparationError::
					OverlayRootUnavailable &&
				linkedOverlay.fieldPath ==
					"overlayRoot",
			"a private overlay link remains invalid while formal logical links remain open") &&
			ok;
		std::error_code overlayRemoveError;
		fs::remove(
			privateOverlayLink,
			overlayRemoveError);
		ok = check(
			!overlayRemoveError,
			"remove only the private overlay link") &&
			ok;
	}

	removeError.clear();
	fs::remove(currentCollection, removeError);
	ok = check(
		!removeError,
		"remove only the formal collection link") &&
		ok;
	return ok;
}

bool testDistinctLogicalPackAliases(const fs::path& root)
{
	const fs::path collection =
		root / "distinct-logical-pack-aliases";
	const fs::path generationA =
		root / "logical-pack-generation-a";
	const fs::path generationB =
		root / "logical-pack-generation-b";
	const fs::path aliasA = collection / "alias-a";
	const fs::path aliasB = collection / "alias-b";
	bool ok = check(
		fs::create_directories(collection) &&
		writeText(
				generationA / "game_profile.ini",
				"[Game]\n"
				"Id=SHARED\n"
				"Name=Physical A\n"
				"Type=0\n") &&
			writeText(
				generationA / "map/scene.map",
				"generation-a-map") &&
			writeText(
				generationB / "game_profile.ini",
				"[Game]\n"
				"Id=SHARED\n"
				"Name=Physical B\n"
				"Type=0\n") &&
			writeText(
				generationB / "map/scene.map",
				"generation-b-map"),
		"create two logical pack aliases and their physical generations");
	ok = check(
		createDirectorySymlink(generationA, aliasA) &&
			createDirectorySymlink(generationA, aliasB),
		"create two distinct logical pack aliases to one current target") &&
		ok;

	const RuntimeResource::ExactSelectionResult selectedA =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			collection, "pack.alias-a");
	const RuntimeResource::ExactSelectionResult selectedB =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			collection, "pack.alias-b");
	ok = check(
		!selectedA.succeeded() &&
			!selectedB.succeeded() &&
			selectedA.error == RuntimeResource::ExactSelectionError::
				ActiveResourcePackIdAmbiguous &&
			selectedB.error == RuntimeResource::ExactSelectionError::
				ActiveResourcePackIdAmbiguous,
		"duplicate Game.Id aliases are discovered but neither path is activated") &&
		ok;

	std::error_code removeError;
	fs::remove(aliasB, removeError);
	const bool aliasBRepointed =
		!removeError &&
		createDirectorySymlink(generationB, aliasB);
	ok = check(
		aliasBRepointed,
		"repoint only logical pack B to generation B") &&
		ok;
	const RuntimeResource::ExactSelectionResult repointedB =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			collection, "pack.alias-b");
	ok = check(
		!repointedB.succeeded() &&
			repointedB.error == RuntimeResource::ExactSelectionError::
				ActiveResourcePackIdAmbiguous,
		"repointing one alias does not bypass duplicate Game.Id rejection") &&
		ok;

	removeError.clear();
	fs::remove(aliasA, removeError);
	std::error_code removeBError;
	fs::remove(aliasB, removeBError);
	ok = check(
		!removeError && !removeBError,
		"remove only the two logical pack aliases") &&
		ok;
	return ok;
}

bool testRuntimePolicyAndDerivedFields(const fs::path& root)
{
	const fs::path collection = root / "runtime-policy";
	bool ok = writeText(
		collection / "resources.ini",
		"[Pack.MOD]\nId=MOD\nPath=mod\n"
		"[Pack.BASE]\nId=BASE\nPath=base\n"
			"[Pack.UIBASE]\nId=UIBASE\nPath=ui-base\n"
			"[Pack.LEGACY]\nId=LEGACY\nPath=legacy\n"
			"[Pack.ORPHAN]\nId=ORPHAN\nPath=orphan\n"
			"[Pack.BADUI]\nId=BADUI\nPath=bad-ui\n"
		"[Pack.MISSUI]\nId=MISSUI\nPath=missing-ui\n"
		"[Pack.UICYCLEA]\nId=UICYCLEA\nPath=ui-cycle-a\n"
		"[Pack.UICYCLEB]\nId=UICYCLEB\nPath=ui-cycle-b\n"
		"[Pack.DUPSAVEA]\nId=DUPSAVEA\nPath=dup-save-a\n"
		"[Pack.DUPSAVEB]\nId=DUPSAVEB\nPath=dup-save-b\n") &&
		writeText(
			collection / "mod/game_profile.ini",
			"[Game]\nId=MOD\nName=Derived Mod\n"
			"[Resource]\nDependencyId=BASE\n"
			"[UI]\nBaseId=UIBASE\n") &&
		writeText(
			collection / "base/game_profile.ini",
			"[Game]\nId=BASE\nName=Base\nType=2\n") &&
		writeText(
			collection / "ui-base/game_profile.ini",
			"[Game]\nId=UIBASE\nName=UI Base\nType=0\n"
			"[UI]\nProfile=yycs\n") &&
		writeText(
			collection / "legacy/game_profile.ini",
			"[Game]\nId=LEGACY\nName=Legacy\n"
			"[Resource]\nDependencyId=BASE\n") &&
		writeText(
			collection / "orphan/game_profile.ini",
			"[Game]\nId=ORPHAN\nName=Orphan\n") &&
		writeText(
			collection / "bad-ui/game_profile.ini",
			"[Game]\nId=BADUI\nName=Bad UI\nType=0\n"
			"[UI]\nProfile=unsupported\n") &&
		writeText(
			collection / "missing-ui/game_profile.ini",
			"[Game]\nId=MISSUI\nName=Missing UI\nType=0\n"
			"[UI]\nBaseId=NOPE\n") &&
		writeText(
			collection / "ui-cycle-a/game_profile.ini",
			"[Game]\nId=UICYCLEA\nName=UI Cycle A\nType=0\n"
			"[UI]\nBaseId=UICYCLEB\n") &&
		writeText(
			collection / "ui-cycle-b/game_profile.ini",
			"[Game]\nId=UICYCLEB\nName=UI Cycle B\nType=1\n"
			"[UI]\nBaseId=UICYCLEA\n") &&
		writeText(
			collection / "dup-save-a/game_profile.ini",
			"[Game]\nId=DUPSAVEA\nName=Duplicate Save A\nType=0\n"
			"[Save]\nNamespace=shared.save\n") &&
		writeText(
			collection / "dup-save-b/game_profile.ini",
			"[Game]\nId=DUPSAVEB\nName=Duplicate Save B\nType=1\n"
			"[Save]\nNamespace=shared:save\n");

	const RuntimeResource::ExactSelectionResult selected =
		RuntimeResource::resolveExactResourceSelection(
			collection, "mod");
	ok = check(
		selected.succeeded() &&
			selected.selection.activeManifest.typeDefined &&
			selected.selection.activeManifest.type == 2 &&
			selected.selection.activeManifest.uiProfile == "YYCS" &&
			selected.selection.activeManifest.name ==
				"Derived Mod",
		"runtime policy takes Game.Type from the content graph and an explicit, conflicting UI.Profile from UI.BaseId") &&
		ok;

	const RuntimeResource::ExactSelectionResult legacy =
		RuntimeResource::resolveExactResourceSelection(
			collection, "LEGACY");
	ok = check(
		legacy.succeeded() &&
			legacy.selection.activeManifest.typeDefined &&
			legacy.selection.activeManifest.type == 2 &&
			legacy.selection.activeManifest.uiProfile ==
				"XJXQY",
		"a pack without UI configuration materializes the default UI for its effective Game.Type") &&
		ok;

	const RuntimeResource::ExactSelectionResult orphan =
		RuntimeResource::resolveExactResourceSelection(
			collection, "ORPHAN");
	ok = check(
		orphan.succeeded() &&
			orphan.selection.activeManifest.typeDefined &&
			orphan.selection.activeManifest.type == 0 &&
			orphan.selection.activeManifest.uiProfile ==
				"JXQY2" &&
			hasCatalogDiagnostic(
				orphan,
				"resource.catalog.game_type_defaulted"),
		"a standalone custom pack without dependencies remains runnable with stable defaults") &&
		ok;

	const RuntimeResource::ExactSelectionResult badUi =
		RuntimeResource::resolveExactResourceSelection(
			collection, "BADUI");
	ok = check(
		badUi.succeeded() &&
			badUi.selection.activeManifest.uiProfile ==
				"JXQY2" &&
			hasCatalogDiagnostic(
				badUi,
				"resource.catalog.ui_profile_defaulted"),
		"an unsupported UI.Profile falls back without disabling local content") &&
		ok;

	const RuntimeResource::ExactSelectionResult missingUi =
		RuntimeResource::resolveExactResourceSelection(
			collection, "MISSUI");
	ok = check(
		missingUi.succeeded() &&
			missingUi.selection.activeManifest.uiProfile ==
				"JXQY2" &&
			hasCatalogDiagnostic(
				missingUi,
				"resource.catalog.ui_dependency_not_found"),
		"a missing explicit UI.BaseId falls back without disabling local content") &&
		ok;

	const RuntimeResource::ExactSelectionResult cyclicUi =
		RuntimeResource::resolveExactResourceSelection(
			collection, "UICYCLEA");
	ok = check(
		cyclicUi.succeeded() &&
			cyclicUi.selection.activeManifest.uiProfile ==
				"YYCS" &&
			hasCatalogDiagnostic(
				cyclicUi,
				"resource.catalog.ui_dependency_cycle_ignored"),
		"a UI.BaseId cycle is isolated while the first referenced pack still supplies a deterministic default UI") &&
		ok;

	const RuntimeResource::ExactSelectionResult duplicateSave =
		RuntimeResource::resolveExactResourceSelection(
			collection, "DUPSAVEA");
	const RuntimeResource::ExactSelectionResult duplicateSavePeer =
		RuntimeResource::resolveExactResourceSelection(
			collection, "DUPSAVEB");
	ok = check(
		duplicateSave.succeeded() &&
			duplicateSave.selection.effectiveSaveNamespace ==
				"shared.save" &&
			hasCatalogDiagnostic(
				duplicateSave,
				"resource.catalog.save_namespace_conflict"),
		"the first portable Save.Namespace collision owner keeps the declared namespace") &&
		ok;
	ok = check(
		duplicateSavePeer.succeeded() &&
			duplicateSavePeer.selection.effectiveSaveNamespace ==
				"shared:save--pack_dup-save-b" &&
			hasCatalogDiagnostic(
				duplicateSavePeer,
				"resource.catalog.save_namespace_conflict"),
		"the later portable Save.Namespace collision owner receives a stable entry-key suffix") &&
		ok;
	return ok;
}

bool testPolicyFixpoint(const fs::path& root)
{
	const fs::path collection = root / "policy-fixpoint";
	bool ok = writeText(
		collection / "resources.ini",
		"[Pack.ACTIVE]\nId=ACTIVE\nPath=active\n"
		"[Pack.BADUI]\nId=BADUI\nPath=bad-ui\n"
		"[Pack.GOODUI]\nId=GOODUI\nPath=good-ui\n") &&
		writeText(
			collection / "active/game_profile.ini",
			"[Game]\nId=ACTIVE\nName=Active\n"
			"[Resource]\nDependencyId=BADUI\n"
			"[UI]\nBaseId=GOODUI\n") &&
		writeText(
			collection / "bad-ui/game_profile.ini",
			"[Game]\nId=BADUI\nName=Bad UI\nType=0\n"
			"[UI]\nProfile=unsupported\n") &&
		writeText(
			collection / "good-ui/game_profile.ini",
			"[Game]\nId=GOODUI\nName=Good UI\nType=0\n"
			"[UI]\nProfile=JXQY2\n");

	const RuntimeResource::ExactSelectionResult result =
		RuntimeResource::resolveExactResourceSelection(
			collection, "ACTIVE");
	ok = check(
		result.succeeded() &&
			result.selection.orderedContentRoots.size() == 2 &&
			result.selection.orderedContentRoots[1].root ==
				fs::canonical(collection / "bad-ui") &&
			result.selection.orderedUiFallbackRoots.size() == 1 &&
			result.selection.orderedUiFallbackRoots[0] ==
				fs::canonical(collection / "good-ui") &&
			result.selection.activeManifest.uiProfile ==
				"JXQY2",
		"an invalid content parent's UI metadata does not cascade into an independently configured UI fallback") &&
		ok;
	return ok;
}

bool testCommonParity(const fs::path& root)
{
	bool ok = true;
	const fs::path multi = root / "common-multi";
	ok = writeText(
		multi / "game_profile.ini",
		"[Game]\nId=ROOT\nName=Root\nType=0\n") && ok;
	ok = writeText(
		multi / "resources.ini",
		"[Pack.OTHER]\nId=OTHER\nPath=other\n") && ok;
	ok = writeText(
		multi / "other/game_profile.ini",
		"[Game]\nId=OTHER\nName=Other\nType=1\n") && ok;
	ok = writeText(
		multi / "common/map/multi.map", "multi-common") && ok;
	const EditorRun::ResourcePreparationResult multiResult =
		EditorRun::prepareEditorRunResources(
			makeDescriptor(multi, "ROOT", "map/multi.map"),
			"1.4.3");
	if (!multiResult.succeeded() ||
		multiResult.prepared.orderedSearchRoots.size() != 1)
	{
		std::cerr << "common parity detail: error=" <<
			static_cast<int>(multiResult.error) <<
			" code=" << multiResult.diagnosticCode <<
			" roots=" <<
			multiResult.prepared.orderedSearchRoots.size() <<
			" index=" <<
			multiResult.prepared.target.map.searchRootIndex <<
			"\n";
	}
	ok = check(
		multiResult.succeeded() &&
			multiResult.prepared.orderedSearchRoots.size() == 1 &&
			multiResult.prepared.resources.commonResourceRoot ==
				fs::canonical(multi / "common") &&
			multiResult.prepared.target.map.searchRootIndex ==
				EditorRun::SearchAllResourceRoots,
		"a collection-root pack keeps Common separate from ordinary content lookup") &&
		ok;

	const fs::path singleParent = root / "common-single";
	const fs::path single = singleParent / "collection";
	ok = writeText(
		single / "game_profile.ini",
		"[Game]\nId=SINGLE\nName=Single\nType=0\n") && ok;
	ok = writeText(
		single / "common/map/single.map",
		"single-common") && ok;
	const EditorRun::ResourcePreparationResult singleResult =
		EditorRun::prepareEditorRunResources(
			makeDescriptor(single, "SINGLE", "map/single.map"),
			"1.4.3");
	ok = check(
		singleResult.succeeded() &&
			singleResult.prepared.orderedSearchRoots.size() == 1 &&
			singleResult.prepared.resources.commonResourceRoot ==
				fs::canonical(single / "common") &&
			singleResult.prepared.target.map.searchRootIndex ==
				EditorRun::SearchAllResourceRoots,
		"a sole collection-root manifest keeps its collection Common as a dedicated root") &&
		ok;

	const fs::path filteredParent = root / "common-filtered";
	const fs::path filtered = filteredParent / "collection";
	ok = writeText(
		filtered / "game_profile.ini",
		"[Game]\nId=FILTERED\nName=Filtered\nType=0\n") && ok;
	ok = writeText(
		filtered / "resources.ini",
		"[Pack.ORPHAN]\nId=ORPHAN\nPath=orphan\n") && ok;
	ok = writeText(
		filtered / "orphan/game_profile.ini",
		"[Game]\nId=ORPHAN\nName=Orphan\n") && ok;
	ok = writeText(
		filtered / "common/map/filtered.map",
		"filtered-common") && ok;
	const EditorRun::ResourcePreparationResult filteredResult =
		EditorRun::prepareEditorRunResources(
			makeDescriptor(
				filtered,
				"FILTERED",
				"map/filtered.map"),
			"1.4.3");
	ok = check(
		filteredResult.succeeded() &&
			filteredResult.prepared.orderedSearchRoots.size() == 1 &&
			filteredResult.prepared.resources.commonResourceRoot ==
				fs::canonical(filtered / "common"),
		"filtered standalone peers do not add Common to ordinary content lookup") &&
		ok;
	return ok;
}

bool testAllOwnerDisablement(const fs::path& root)
{
	const fs::path parent = root / "all-owner-disablement";
	const fs::path collection = parent / "collection";
	bool ok = writeText(
		collection / "game_profile.ini",
		"[Game]\nId=ACTIVE\nName=Active\nType=0\n"
		"[Save]\nNamespace=active-save\n") &&
		writeText(
			collection / "resources.ini",
			"[Pack.DUPEA]\nId=DUPE\nPath=dupe-a\n"
			"[Pack.DUPEB]\nId=dupe\nPath=dupe-b\n"
			"[Pack.SAVEA]\nId=SAVEA\nPath=save-a\n"
			"[Pack.SAVEB]\nId=SAVEB\nPath=save-b\n") &&
		writeText(
			collection / "dupe-a/game_profile.ini",
			"[Game]\nId=DUPE\nName=Duplicate A\nType=0\n"
			"[Save]\nNamespace=dupe-a-save\n") &&
		writeText(
			collection / "dupe-b/game_profile.ini",
			"[Game]\nId=dupe\nName=Duplicate B\nType=1\n"
			"[Save]\nNamespace=dupe-b-save\n") &&
		writeText(
			collection / "save-a/game_profile.ini",
			"[Game]\nId=SAVEA\nName=Save A\nType=0\n"
			"[Save]\nNamespace=shared.save\n") &&
		writeText(
			collection / "save-b/game_profile.ini",
			"[Game]\nId=SAVEB\nName=Save B\nType=1\n"
			"[Save]\nNamespace=shared:save\n") &&
		writeText(
			collection / "common/map/owner-proof.map",
			"owner-proof");

	const RuntimeResource::ExactSelectionResult duplicateId =
		RuntimeResource::resolveExactResourceSelection(
			collection, "DUPE");
	ok = check(
		!duplicateId.succeeded() &&
			duplicateId.error == RuntimeResource::ExactSelectionError::
				ActiveResourcePackIdAmbiguous &&
			duplicateId.diagnosticCode ==
				"resource.catalog.active_id_ambiguous",
		"duplicate Game.Id owners are reported and cannot be activated") &&
		ok;

	const std::array<std::pair<std::string, std::string>, 2>
		saveExpectations = {{
			{ "SAVEA", "shared.save" },
			{ "SAVEB", "shared:save--pack_save-b" }
		}};
	for (const auto& expectation : saveExpectations)
	{
		const RuntimeResource::ExactSelectionResult duplicateSave =
			RuntimeResource::resolveExactResourceSelection(
				collection, expectation.first);
		ok = check(
			duplicateSave.succeeded() &&
				duplicateSave.selection.
					effectiveSaveNamespace ==
						expectation.second &&
				hasCatalogDiagnostic(
					duplicateSave,
					"resource.catalog.save_namespace_conflict"),
			"every portable Save.Namespace collision owner remains visible with a deterministic effective namespace") &&
			ok;
	}

	const EditorRun::ResourcePreparationResult active =
		EditorRun::prepareEditorRunResources(
			makeDescriptor(
				collection,
				"ACTIVE",
				"map/owner-proof.map"),
			"1.4.3");
	ok = check(
		active.succeeded() &&
			active.prepared.orderedSearchRoots.size() == 1 &&
			active.prepared.resources.commonResourceRoot ==
				fs::canonical(collection / "common") &&
			active.prepared.target.map.searchRootIndex ==
				EditorRun::SearchAllResourceRoots,
		"duplicate-ID and namespace handling keeps Common outside ordinary content lookup") &&
		ok;
	return ok;
}

std::string makeLargeManifest(
	const std::string& id,
	std::size_t targetBytes)
{
	std::string manifest =
		"[Game]\nId=" + id +
		"\nName=Large\nType=0\n";
	const std::string comment =
		";" + std::string(900, 'x') + "\n";
	while (manifest.size() + comment.size() <= targetBytes)
	{
		manifest += comment;
	}
	if (manifest.size() < targetBytes)
	{
		const std::size_t remainingBytes =
			targetBytes - manifest.size();
		manifest += ";";
		manifest.append(
			remainingBytes - 1, 'x');
	}
	return manifest;
}

bool createDependencyChain(
	const fs::path& collection,
	std::size_t packCount)
{
	std::string index;
	for (std::size_t packIndex = 0;
		packIndex < packCount;
		++packIndex)
	{
		const std::string directoryName =
			"p" + std::to_string(1000 + packIndex);
		const std::string id =
			"P" + std::to_string(1000 + packIndex);
		index += "[Pack." + id + "]\nId=" + id +
			"\nPath=" + id + "\n";
		std::string manifest =
			"[Game]\nId=" + id + "\nName=" + id + "\n";
		if (packIndex + 1 == packCount)
		{
			manifest += "Type=0\n";
		}
		else
		{
			manifest +=
				"[Resource]\nDependencyId=P" +
				std::to_string(1001 + packIndex) + "\n";
		}
		if (!writeText(
				collection / directoryName /
					"game_profile.ini",
				manifest))
		{
			return false;
		}
	}
	return writeText(collection / "resources.ini", index);
}

bool createRealPackLimitFixture(const fs::path& collection)
{
	const std::size_t indexedPackCount =
		RuntimeResource::MaximumCatalogPackCount - 1;
	std::string index;
	for (std::size_t packIndex = 0;
		packIndex < indexedPackCount;
		++packIndex)
	{
		const std::string directoryName =
			"l" + std::to_string(10000 + packIndex);
		const std::string id =
			"L" + std::to_string(10000 + packIndex);
		index += "[Pack." + id + "]\nId=" + id +
			"\nPath=" + id + "\n";
		std::string manifest =
			"[Game]\nId=" + id + "\nName=" + id + "\n";
		if (packIndex == 0)
		{
			manifest +=
				"[Resource]\n"
				"DependencyId=EXTERNAL1024\n";
		}
		else
		{
			manifest += "Type=0\n";
		}
		if (!writeText(
				collection / directoryName /
					"game_profile.ini",
				manifest))
		{
			return false;
		}
	}
	return writeText(collection / "resources.ini", index) &&
		writeText(
			collection / "external-1024/game_profile.ini",
			"[Game]\n"
			"Id=EXTERNAL1024\n"
			"Name=External 1024\n"
			"Type=0\n");
}

bool testCatalogLimits(const fs::path& root)
{
	bool ok = check(
		RuntimeResource::MaximumCatalogPackCount == 1024 &&
			RuntimeResource::MaximumCatalogDependencyDepth == 128 &&
			RuntimeResource::MaximumCatalogIniBytes ==
				16 * 1024 * 1024,
		"catalog limits are explicit and stable");

	const fs::path packLimit = root / "catalog-pack-limit";
	ok = check(
		createRealPackLimitFixture(packLimit),
		"create 1023 indexed manifests and one additional dependency manifest") &&
		ok;
	const RuntimeResource::ExactSelectionResult maximumPackResult =
		RuntimeResource::resolveExactResourceSelection(
			packLimit, "L10000");
	ok = check(
		maximumPackResult.succeeded() &&
			maximumPackResult.selection.orderedContentRoots.size() ==
				2 &&
			folderName(
				maximumPackResult.selection.
					orderedContentRoots[1].root) ==
				"external-1024",
		"exactly 1024 real manifest-bearing roots remain valid") &&
		ok;

	ok = writeText(
		packLimit / "external-1024/game_profile.ini",
		"[Game]\n"
		"Id=EXTERNAL1024\n"
		"Name=External 1024\n"
		"[Resource]\n"
		"DependencyId=EXTERNAL1025\n") &&
		writeText(
			packLimit / "external-1025/game_profile.ini",
			"[Game]\n"
			"Id=EXTERNAL1025\n"
			"Name=External 1025\n"
			"Type=0\n") &&
		ok;
	const EditorRun::ResourcePreparationResult tooManyResult =
		EditorRun::prepareEditorRunResources(
			makeDescriptor(
				packLimit, "L10000", "map/unused.map"),
			"1.4.3");
	ok = check(
		tooManyResult.succeeded() &&
			tooManyResult.prepared.resources.
				orderedContentRoots.empty() &&
			tooManyResult.diagnosticCode ==
				"editor_run.resource.catalog_pack_limit_exceeded",
		"an over-limit dependency catalog is skipped as an empty formal route") &&
		ok;

	const fs::path maximumDepth = root / "catalog-depth-ok";
	ok = check(
		createDependencyChain(
			maximumDepth,
			RuntimeResource::MaximumCatalogDependencyDepth),
		"create maximum-depth dependency fixture") && ok;
	const RuntimeResource::ExactSelectionResult maximumDepthResult =
		RuntimeResource::resolveExactResourceSelection(
			maximumDepth, "P1000");
	ok = check(
		maximumDepthResult.succeeded(),
		"exactly 128 dependency graph layers remain valid") && ok;

	const fs::path tooDeep = root / "catalog-depth-limit";
	ok = check(
		createDependencyChain(
			tooDeep,
			RuntimeResource::MaximumCatalogDependencyDepth + 1),
		"create over-depth dependency fixture") && ok;
	const EditorRun::ResourcePreparationResult tooDeepResult =
		EditorRun::prepareEditorRunResources(
			makeDescriptor(
				tooDeep, "P1000", "map/unused.map"),
			"1.4.3");
	ok = check(
		tooDeepResult.succeeded() &&
			tooDeepResult.prepared.resources.
				orderedContentRoots.empty() &&
			tooDeepResult.diagnosticCode ==
				"editor_run.resource.dependency_depth_limit_exceeded",
		"an over-depth dependency branch is skipped as an empty formal route") &&
		ok;

	return ok;
}

bool testCatalogIniByteLimits(const fs::path& root)
{
	constexpr std::size_t maximumIniFileBytes = 1024 * 1024;
	bool ok = true;

	const fs::path longSingleLine =
		root / "catalog-long-single-line";
	const std::string longName(4096, 'n');
	ok = writeText(
		longSingleLine / "game_profile.ini",
		"[Game]\nId=LONGLINE\nName=" +
			longName + "\nType=0\n") &&
		ok;
	const RuntimeResource::ExactSelectionResult
		longSingleLineResult =
			RuntimeResource::resolveExactResourceSelection(
				longSingleLine, "LONGLINE");
	ok = check(
		longSingleLineResult.succeeded() &&
			longSingleLineResult.selection.
				activeManifest.name == longName,
		"runtime exact selection accepts the same 4 KiB single INI line as editor preparation") &&
		ok;

	const fs::path embeddedNul =
		root / "catalog-embedded-nul";
	std::string embeddedNulManifest =
		"[Game]\nId=NUL\nName=Before";
	embeddedNulManifest.push_back('\0');
	embeddedNulManifest +=
		"After\nType=0\n";
	ok = writeText(
		embeddedNul / "game_profile.ini",
		embeddedNulManifest) && ok;
	const RuntimeResource::ExactSelectionResult
		embeddedNulResult =
			RuntimeResource::resolveExactResourceSelection(
				embeddedNul, "NUL");
	ok = check(
		embeddedNulResult.succeeded() &&
			embeddedNulResult.selection.
				activeManifest.id == "NUL" &&
			embeddedNulResult.selection.
				activeManifest.name.empty() &&
			hasCatalogDiagnostic(
				embeddedNulResult,
				"resource.catalog.root_manifest_sanitized"),
		"runtime exact selection isolates an embedded-NUL metadata line while retaining usable identity and local content") &&
		ok;

	const fs::path perFile = root / "catalog-file-byte-limit";
	ok = writeText(
		perFile / "game_profile.ini",
		makeLargeManifest("FILELIMIT", maximumIniFileBytes)) &&
		ok;
	const RuntimeResource::ExactSelectionResult exactFile =
		RuntimeResource::resolveExactResourceSelection(
			perFile, "FILELIMIT");
	ok = check(
		exactFile.succeeded(),
		"an exactly 1 MiB manifest remains valid") && ok;

	ok = writeText(
		perFile / "game_profile.ini",
		makeLargeManifest(
			"FILELIMIT", maximumIniFileBytes + 1)) &&
		ok;
	const RuntimeResource::ExactSelectionResult oversizedFile =
		RuntimeResource::resolveExactResourceSelection(
			perFile, "FILELIMIT");
	if (oversizedFile.error !=
			RuntimeResource::ExactSelectionError::
				ActiveResourcePackIdNotFound)
	{
		const RootedResourceReader::Result directRead =
			RootedResourceReader::readBoundedFileFromRoot(
				perFile,
				"game_profile.ini",
				maximumIniFileBytes);
		std::cerr << "oversized manifest error="
			<< static_cast<int>(oversizedFile.error)
			<< " code=" << oversizedFile.diagnosticCode
			<< " message=" << oversizedFile.message
			<< " size=" << fs::file_size(
				perFile / "game_profile.ini")
			<< " directStatus="
			<< static_cast<int>(directRead.status) << "\n";
	}
	ok = check(
		oversizedFile.error ==
			RuntimeResource::ExactSelectionError::
				ActiveResourcePackIdNotFound &&
			oversizedFile.diagnosticCode ==
				"editor_run.resource.id_not_found" &&
			hasCatalogDiagnostic(
				oversizedFile,
				"resource.catalog.root_manifest_invalid"),
		"a 1 MiB plus one byte manifest is skipped without parsing while retaining its entry diagnostic") &&
		ok;

	const fs::path cumulative = root / "catalog-cumulative-byte-limit";
	constexpr std::size_t manifestCount = 16;
	std::string indexText;
	for (std::size_t packIndex = 0;
		packIndex < manifestCount;
		++packIndex)
	{
		const std::string id =
			"B" + std::to_string(100 + packIndex);
		indexText += "[Pack." + id + "]\nId=" + id +
			"\nPath=" + id + "\n";
	}
	ok = check(
		indexText.size() < maximumIniFileBytes,
		"the cumulative-budget index fits inside one INI file") &&
		ok;
	ok = writeText(cumulative / "resources.ini", indexText) && ok;
	for (std::size_t packIndex = 0;
		packIndex < manifestCount;
		++packIndex)
	{
		const std::string directoryName =
			"b" + std::to_string(100 + packIndex);
		const std::string id =
			"B" + std::to_string(100 + packIndex);
		const std::size_t manifestBytes =
			packIndex + 1 == manifestCount
			? maximumIniFileBytes - indexText.size()
			: maximumIniFileBytes;
		ok = writeText(
			cumulative / directoryName /
				"game_profile.ini",
			makeLargeManifest(id, manifestBytes)) &&
			ok;
	}
	const RuntimeResource::ExactSelectionResult exactBudget =
		RuntimeResource::resolveExactResourceSelection(
			cumulative, "B100");
	ok = check(
		exactBudget.succeeded(),
		"resources.ini plus manifests may total exactly 16 MiB") &&
		ok;

	const std::string lastId =
		"B" + std::to_string(100 + manifestCount - 1);
	const std::string lastDirectoryName =
		"b" + std::to_string(100 + manifestCount - 1);
	ok = writeText(
		cumulative / lastDirectoryName /
			"game_profile.ini",
		makeLargeManifest(
			lastId,
			maximumIniFileBytes - indexText.size() + 1)) &&
		ok;
	const RuntimeResource::ExactSelectionResult overBudget =
		RuntimeResource::resolveExactResourceSelection(
			cumulative, "B100");
	if (overBudget.error !=
			RuntimeResource::ExactSelectionError::
				CatalogIniBudgetExceeded)
	{
		std::cerr << "cumulative budget error="
			<< static_cast<int>(overBudget.error)
			<< " code=" << overBudget.diagnosticCode
			<< " message=" << overBudget.message
			<< " lastSize=" << fs::file_size(
				cumulative / lastDirectoryName /
					"game_profile.ini")
			<< " indexSize=" << indexText.size() << "\n";
	}
	ok = check(
		overBudget.error ==
			RuntimeResource::ExactSelectionError::
				CatalogIniBudgetExceeded &&
			overBudget.diagnosticCode ==
				"editor_run.resource.catalog_ini_budget_exceeded",
		"the cumulative 16 MiB plus one byte boundary has a structured failure") &&
		ok;
	return ok;
}

bool testExplicitCommonFailure(const fs::path& root)
{
	const fs::path collection = root / "missing-common";
	bool ok = writeText(
		collection / "game_profile.ini",
		"[Game]\nId=MOD\nName=Mod\nType=0\n") &&
		writeText(
			collection / "resources.ini",
			"[Collection]\nCommonPath=missing-common\n");
	const RuntimeResource::ExactSelectionResult result =
		RuntimeResource::resolveExactResourceSelection(
			collection, "MOD");
	ok = check(
		result.succeeded() &&
			result.selection.orderedContentRoots.size() == 1 &&
			result.selection.commonResourceRoot.empty() &&
			hasCatalogDiagnostic(
				result,
				"resource.catalog.common_root_unavailable"),
		"an explicitly configured missing Common root disables only the Common fallback") &&
		ok;
	return ok;
}
}

int main()
{
	const fs::path root = makeUniqueTestRoot();
	std::error_code error;
	fs::remove_all(root, error);
	fs::create_directories(root, error);
	if (error)
	{
		std::cerr << "FAIL: could not create test root\n";
		return 1;
	}

	bool ok = true;
	ok = testProfileMarkedJxqy2(root) && ok;
	ok = testExactSelectionAndOrder(root) && ok;
	ok = testConfiguredHostPathSemantics(root) && ok;
	ok = testIdAndDependencyFailures(root) && ok;
	ok = testCompatibility(root) && ok;
	ok = testDeferredTargetLookup(root) && ok;
	ok = testRuntimeResourceRoutingContractHandoff(root) &&
		ok;
	ok = testFormalCollectionRootRedirect(root) && ok;
	ok = testDistinctLogicalPackAliases(root) && ok;
	ok = testRuntimePolicyAndDerivedFields(root) && ok;
	ok = testPolicyFixpoint(root) && ok;
	ok = testCommonParity(root) && ok;
	ok = testAllOwnerDisablement(root) && ok;
	ok = testCatalogLimits(root) && ok;
	ok = testCatalogIniByteLimits(root) && ok;
	ok = testExplicitCommonFailure(root) && ok;

	fs::remove_all(root, error);
	if (!ok)
	{
		return 1;
	}
	std::cout <<
		"Editor-run resource routing tests passed\n";
	return 0;
}
