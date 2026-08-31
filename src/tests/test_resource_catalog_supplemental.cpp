#include "../Resource/ResourceCatalog.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;

bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
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
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output)
	{
		return false;
	}
	output.write(
		text.data(), static_cast<std::streamsize>(text.size()));
	return output.good();
}

fs::path resolvedPath(const fs::path& path)
{
	std::error_code error;
	fs::path absolute = fs::absolute(path, error);
	return error ? path.lexically_normal() : absolute.lexically_normal();
}

const RuntimeResource::ResourceCatalogEntry* findEntry(
	const RuntimeResource::ResourceCatalogSnapshot& snapshot,
	const std::string& stableKey)
{
	const auto found = std::find_if(
		snapshot.entries.begin(), snapshot.entries.end(),
		[&stableKey](
			const RuntimeResource::ResourceCatalogEntry& entry)
		{
			return entry.stableKey == stableKey;
		});
	return found == snapshot.entries.end() ? nullptr : &*found;
}

bool hasDiagnostic(
	const std::vector<RuntimeResource::CatalogDiagnostic>& diagnostics,
	const std::string& code)
{
	return std::any_of(
		diagnostics.begin(), diagnostics.end(),
		[&code](const RuntimeResource::CatalogDiagnostic& diagnostic)
		{
			return diagnostic.code == code;
		});
}

std::vector<std::string> entryKeys(
	const RuntimeResource::ResourceCatalogSnapshot& snapshot)
{
	std::vector<std::string> result;
	result.reserve(snapshot.entries.size());
	for (const RuntimeResource::ResourceCatalogEntry& entry :
		snapshot.entries)
	{
		result.push_back(entry.stableKey);
	}
	return result;
}

std::vector<std::string> effectiveNamespaces(
	const RuntimeResource::ResourceCatalogSnapshot& snapshot)
{
	std::vector<std::string> result;
	result.reserve(snapshot.entries.size());
	for (const RuntimeResource::ResourceCatalogEntry& entry :
		snapshot.entries)
	{
		result.push_back(entry.effectiveSaveNamespace);
	}
	return result;
}

RuntimeResource::ResourceCatalogRequest makeRequest(
	const fs::path& root)
{
	RuntimeResource::ResourceCatalogRequest request;
	request.primaryCollectionRoot = root / "primary";
	request.supplementalRoots = {
		{
			root / "external/z-mod",
			"external:mod",
			"android-external"
		},
		{
			root / "external/missing-directory",
			"external:missing-directory",
			"android-external"
		},
		{
			root / "external/c-single",
			"external:single",
			"android-external"
		},
		{
			root / "external/a-duplicate",
			"external:duplicate",
			"android-external"
		},
		{
			root / "external/f-invalid-profile",
			"external:invalid-profile",
			"android-external"
		},
		{
			root / "external/b-support",
			"external:support-duplicate-root",
			"android-external"
		},
		{
			root / "external/g-key-conflict",
			"pack.base",
			"android-external"
		},
		{
			root / "external/b-support",
			"external:support",
			"android-external"
		},
		{
			root / "external/h-blank-key",
			"  ",
			"android-external"
		},
		{
			root / "external/e-no-root-profile",
			"external:no-root-profile",
			"android-external"
		},
		{
			root / "external/d-explicit-common",
			"external:explicit-common",
			"android-external"
		}
	};
	return request;
}

bool createFixture(const fs::path& root)
{
	bool ok = writeText(
		root / "primary/resources.ini",
		"[Collection]\n"
		"CommonPath=common\n"
		"UpdateSourceUrl=https://download.example.test/source.ini\n"
		"ResourceCatalogUrl=https://updates.example.test/resources/catalog.ini\n"
		"ApplicationCatalogUrl=https://updates.example.test/application/catalog.ini\n"
		"\n"
		"[Pack.BASE]\n"
		"Path=base\n") &&
		writeText(
			root / "primary/base/game_profile.ini",
			"[Game]\n"
			"Id=BASE\n"
			"Name=Primary Base\n"
			"Type=1\n"
			"[UI]\n"
			"Profile=YYCS\n"
			"[Save]\n"
			"Namespace=shared\n") &&
		writeText(
			root / "primary/common/config/common.txt",
			"primary common\n") &&
		writeText(
			root / "external/a-duplicate/game_profile.ini",
			"[Game]\n"
			"Id=single\n"
			"Name=External Duplicate\n"
			"Type=0\n"
			"[Save]\n"
			"Namespace=shared\n") &&
		writeText(
			root / "external/b-support/game_profile.ini",
			"[Game]\n"
			"Id=SUPPORT\n"
			"Name=External Support\n"
			"Type=2\n") &&
		writeText(
			root / "external/c-single/game_profile.ini",
			"[Game]\n"
			"Id=SINGLE\n"
			"Name=Single Root\n"
			"Type=0\n") &&
		writeText(
			root / "external/c-single/resources.ini",
			"[Pack.HIDDEN]\nPath=nested\n") &&
		writeText(
			root / "external/c-single/nested/game_profile.ini",
			"[Game]\nId=HIDDEN\nName=Hidden Child\nType=0\n") &&
		writeText(
			root / "external/d-explicit-common/game_profile.ini",
			"[Game]\n"
			"Id=EXPLICIT\n"
			"Name=Explicit Common\n"
			"Type=0\n"
			"[Resource]\n"
			"CommonPath=../explicit-common\n") &&
		writeText(
			root / "external/explicit-common/config/common.txt",
			"external common\n") &&
		writeText(
			root / "external/e-no-root-profile/nested/game_profile.ini",
			"[Game]\nId=NESTED_ONLY\nName=Nested Only\nType=0\n") &&
		writeText(
			root / "external/f-invalid-profile/game_profile.ini",
			"[Game]\nName=Missing Id\nType=0\n") &&
		writeText(
			root / "external/g-key-conflict/game_profile.ini",
			"[Game]\nId=KEY_CONFLICT\nName=Key Conflict\nType=0\n") &&
		writeText(
			root / "external/h-blank-key/game_profile.ini",
			"[Game]\nId=BLANK_KEY\nName=Blank Key\nType=0\n") &&
		writeText(
			root / "external/z-mod/game_profile.ini",
			"[Game]\n"
			"Id=MOD\n"
			"Name=External Mod\n"
			"[Resource]\n"
			"DependencyId=BASE,SUPPORT\n"
			"[UI]\n"
			"BaseId=BASE\n");
	return ok;
}

bool testSnapshotAggregation(const fs::path& root)
{
	RuntimeResource::ResourceCatalogRequest request =
		makeRequest(root);
	const RuntimeResource::ResourceCatalogSnapshotResult result =
		RuntimeResource::loadResourceCatalogSnapshot(request);
	bool ok = check(result.succeeded(),
		"supplemental catalog snapshot succeeds despite bad peers");
	if (!result.succeeded())
	{
		return false;
	}

	const std::vector<std::string> expectedKeys = {
		"pack.base",
		"external:duplicate",
		"external:support",
		"external:single",
		"external:explicit-common",
		"external:mod"
	};
	ok = check(
		entryKeys(result.snapshot) == expectedKeys,
		"primary entries precede supplemental roots sorted by normalized path") && ok;
	ok = check(
		result.snapshot.commonResourceRoot ==
			resolvedPath(root / "primary/common"),
		"snapshot common root remains owned by the primary collection") && ok;
	ok = check(
		result.snapshot.updateSourceUrl ==
			"https://download.example.test/source.ini" &&
		result.snapshot.resourceCatalogUrl ==
			"https://updates.example.test/resources/catalog.ini" &&
		result.snapshot.applicationCatalogUrl ==
			"https://updates.example.test/application/catalog.ini",
		"snapshot exposes the source, resource and application catalog URLs") && ok;

	const RuntimeResource::ResourceCatalogEntry* primary =
		findEntry(result.snapshot, "pack.base");
	const RuntimeResource::ResourceCatalogEntry* duplicate =
		findEntry(result.snapshot, "external:duplicate");
	const RuntimeResource::ResourceCatalogEntry* support =
		findEntry(result.snapshot, "external:support");
	ok = check(
		primary != nullptr && primary->sourceTag.empty() &&
			!primary->saveNamespaceAdjusted &&
			primary->effectiveSaveNamespace == "shared",
		"primary entry keeps its source and unadjusted save namespace") && ok;
	ok = check(
		duplicate != nullptr &&
			duplicate->sourceTag == "android-external" &&
			duplicate->manifest.resourceRoot ==
				resolvedPath(
					root / "external/a-duplicate").generic_u8string() +
					"/" &&
			duplicate->saveNamespaceAdjusted &&
			duplicate->effectiveSaveNamespace ==
				"shared--external_duplicate",
		"supplemental save collision is finalized against the primary entry") && ok;
	ok = check(
		support != nullptr && support->discoveryOrder == 2,
		"supplemental snapshot entry preserves shared discovery order") && ok;

	for (const std::string& diagnosticCode : {
		std::string("resource.catalog.duplicate_game_id"),
		std::string("resource.catalog.supplemental_root_unavailable"),
		std::string("resource.catalog.supplemental_manifest_invalid"),
		std::string("resource.catalog.supplemental_duplicate_root"),
		std::string("resource.catalog.supplemental_stable_key_conflict"),
		std::string("resource.catalog.supplemental_stable_key_invalid")
	})
	{
		ok = check(
			hasDiagnostic(result.snapshot.diagnostics, diagnosticCode),
			"snapshot reports isolated supplemental diagnostic " +
				diagnosticCode) && ok;
	}
	ok = check(
		findEntry(result.snapshot, "pack.hidden") == nullptr &&
			findEntry(result.snapshot, "external:no-root-profile") == nullptr &&
			findEntry(result.snapshot, "pack.base") != nullptr,
		"supplemental resources.ini and nested profiles are not discovered") && ok;

	std::reverse(
		request.supplementalRoots.begin(),
		request.supplementalRoots.end());
	const RuntimeResource::ResourceCatalogSnapshotResult reversed =
		RuntimeResource::loadResourceCatalogSnapshot(request);
	ok = check(
		reversed.succeeded() &&
			entryKeys(reversed.snapshot) == expectedKeys &&
			effectiveNamespaces(reversed.snapshot) ==
				effectiveNamespaces(result.snapshot),
		"supplemental input order does not change entries or save namespaces") && ok;
	return ok;
}

bool testCombinedSelection(const fs::path& root)
{
	const RuntimeResource::ResourceCatalogRequest request =
		makeRequest(root);
	const RuntimeResource::ExactSelectionResult selected =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			request, "external:mod");
	bool ok = check(selected.succeeded(),
		"supplemental entry can be selected by its global stable key");
	if (!selected.succeeded())
	{
		return false;
	}
	const std::vector<RuntimeResource::ContentRoot>& roots =
		selected.selection.orderedContentRoots;
	ok = check(
		roots.size() == 4 &&
			roots[0].kind == RuntimeResource::ContentRootKind::Active &&
			roots[0].root == resolvedPath(root / "external/z-mod") &&
			roots[1].kind == RuntimeResource::ContentRootKind::DependencyId &&
			roots[1].root == resolvedPath(root / "primary/base") &&
			roots[2].kind == RuntimeResource::ContentRootKind::DependencyId &&
			roots[2].root == resolvedPath(root / "external/b-support") &&
			roots[3].kind == RuntimeResource::ContentRootKind::Common &&
			roots[3].root == resolvedPath(root / "primary/common"),
		"selection resolves primary and supplemental dependencies before primary common") && ok;
	ok = check(
		selected.selection.activeManifest.typeDefined &&
			selected.selection.activeManifest.type == 1 &&
			selected.selection.activeManifest.uiProfile == "YYCS",
		"supplemental active pack inherits Game.Type and UI.BaseId from the primary catalog") && ok;
	ok = check(
		!hasDiagnostic(
			selected.diagnostics,
			"resource.catalog.dependency_id_ambiguous"),
		"unrelated duplicate IDs do not affect valid dependency resolution") && ok;

	const RuntimeResource::ExactSelectionResult primaryById =
		RuntimeResource::resolveExactResourceSelection(request, "BASE");
	ok = check(
		primaryById.succeeded() &&
			primaryById.selection.stableActiveEntryKey == "pack.base" &&
			primaryById.selection.activeResourceRoot ==
				resolvedPath(root / "primary/base"),
		"unique primary ID remains selectable when unrelated supplemental IDs conflict") && ok;

	const RuntimeResource::ExactSelectionResult duplicateByKey =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			request, "external:duplicate");
	ok = check(
		!duplicateByKey.succeeded() &&
			duplicateByKey.error == RuntimeResource::
				ExactSelectionError::ActiveResourcePackIdAmbiguous,
		"stable entry key cannot bypass duplicate supplemental Game.Id rejection") && ok;

	const RuntimeResource::ExactSelectionResult hidden =
		RuntimeResource::resolveExactResourceSelection(request, "HIDDEN");
	ok = check(
		hidden.error ==
			RuntimeResource::ExactSelectionError::
				ActiveResourcePackIdNotFound,
		"supplemental resources.ini does not introduce hidden child entries") && ok;

	const RuntimeResource::ExactSelectionResult explicitCommon =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			request, "external:explicit-common");
	ok = check(
		explicitCommon.succeeded() &&
			explicitCommon.selection.commonResourceRoot ==
				resolvedPath(root / "primary/common"),
		"supplemental package metadata does not override the primary collection common") && ok;
	return ok;
}

bool testPrimaryDirectChildrenAreDiscovered(const fs::path& root)
{
	const fs::path collection = root / "automatic";
	bool ok = writeText(
		collection / "resources.ini",
		"[Collection]\n"
		"CommonPath=shared\n"
		"\n"
		"[Pack.DISABLED]\n"
		"Path=disabled\n"
		"Enabled=0\n") &&
		writeText(
			collection / "alpha/game_profile.ini",
			"[Game]\nId=ALPHA\nName=Alpha\nType=0\n") &&
		writeText(
			collection / "beta/game_profile.ini",
			"[Game]\nId=BETA\nName=Beta\nType=0\n") &&
		writeText(
			collection / "disabled/game_profile.ini",
			"[Game]\nId=DISABLED\nName=Disabled\nType=0\n") &&
		writeText(
			collection / "invalid/game_profile.ini",
			"[Game]\nName=Missing Id\nType=0\n") &&
		writeText(
			collection / "shared/game_profile.ini",
			"[Game]\nId=COMMON_WRONG\nName=Common\nType=0\n") &&
		writeText(
			collection / ".staging/package/game_profile.ini",
			"[Game]\nId=STAGING\nName=Staging\nType=0\n") &&
		writeText(
			collection / "nested/child/game_profile.ini",
			"[Game]\nId=NESTED\nName=Nested\nType=0\n");
	if (!check(ok, "create automatic discovery fixture"))
	{
		return false;
	}

	const RuntimeResource::ResourceCatalogSnapshotResult snapshot =
		RuntimeResource::loadResourceCatalogSnapshot(collection);
	ok = check(snapshot.succeeded(),
		"automatic direct-child discovery succeeds") && ok;
	ok = check(
		entryKeys(snapshot.snapshot) ==
			std::vector<std::string>{
				"pack.alpha", "pack.beta", "pack.disabled" },
		"direct child profiles are sorted and do not depend on Enabled") && ok;
	ok = check(
		hasDiagnostic(
			snapshot.snapshot.diagnostics,
			"resource.catalog.discovered_manifest_invalid"),
		"invalid direct child profile remains visible as a diagnostic") && ok;
	ok = check(
		findEntry(snapshot.snapshot, "pack.shared") == nullptr &&
			findEntry(snapshot.snapshot, "pack..staging") == nullptr &&
			findEntry(snapshot.snapshot, "pack.nested") == nullptr,
		"common, hidden transaction, and nested-only directories are not resources") && ok;
	return ok;
}

bool testPackagedRootUsesConventionalCommon()
{
	const std::string manifest =
		"[Game]\n"
		"Id=PACKAGED_ROOT\n"
		"Name=Packaged Root\n"
		"Type=0\n";
	RuntimeResource::ResourceCatalogFileAccess fileAccess;
	fileAccess.readFileFromRoot =
		[manifest](const fs::path& root,
			std::string_view relativePath,
			std::size_t maximumBytes)
		{
			RuntimeResource::CatalogFileReadResult result;
			if (root.empty() && relativePath == "game_profile.ini")
			{
				if (manifest.size() > maximumBytes)
				{
					result.status = RuntimeResource::
						CatalogFileReadStatus::TooLarge;
					return result;
				}
				result.status = RuntimeResource::
					CatalogFileReadStatus::Success;
				result.bytes.assign(manifest.begin(), manifest.end());
				return result;
			}
			result.status = RuntimeResource::
				CatalogFileReadStatus::NotFound;
			return result;
		};
	fileAccess.getDirectoryStatus =
		[](const fs::path& path)
		{
			return path.empty()
				? RuntimeResource::CatalogDirectoryStatus::Exists
				: RuntimeResource::CatalogDirectoryStatus::Unknown;
		};

	RuntimeResource::ResourceCatalogRequest request;
	const RuntimeResource::ResourceCatalogSnapshotResult snapshot =
		RuntimeResource::loadResourceCatalogSnapshot(
			request, fileAccess);
	const RuntimeResource::ExactSelectionResult selected =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			request, "root", fileAccess);
	return check(
		snapshot.succeeded() && snapshot.snapshot.entries.size() == 1 &&
			snapshot.snapshot.entries.front().root.empty() &&
			snapshot.snapshot.commonResourceRoot == fs::path("common") &&
			selected.succeeded() &&
			selected.selection.activeResourceRoot.empty() &&
			selected.selection.commonResourceRoot == fs::path("common") &&
			selected.selection.orderedContentRoots.size() == 2 &&
			selected.selection.orderedContentRoots[1].kind ==
				RuntimeResource::ContentRootKind::Common,
		"a statless packaged root without resources.ini keeps logical common/ available");
}

bool testSupplementalBudgetDoesNotConsumeSelectionBudget()
{
	using RuntimeResource::CatalogDirectoryStatus;
	using RuntimeResource::CatalogFileReadResult;
	using RuntimeResource::CatalogFileReadStatus;
	std::map<std::string, std::string> files;
	std::set<std::string> directories = { "primary" };
	const auto key = [](const fs::path& root, std::string_view relativePath)
	{
		return root.lexically_normal().generic_u8string() + "|" +
			std::string(relativePath);
	};
	files[key("primary", "game_profile.ini")] =
		"[Game]\n"
		"Id=PRIMARY\n"
		"Name=Primary\n"
		"Type=0\n";

	RuntimeResource::ResourceCatalogRequest request;
	request.primaryCollectionRoot = "primary";
	for (int index = 0; index < 18; ++index)
	{
		const std::string root = std::string("external/") +
			(index < 10 ? "0" : "") + std::to_string(index);
		const std::string header =
			"[Game]\nId=EXTERNAL_" + std::to_string(index) +
			"\nName=External\nType=0\n;";
		const std::size_t targetSize = 1024 * 1024 - 32;
		files[key(root, "game_profile.ini")] = header +
			std::string(targetSize - header.size() - 1, 'x') + "\n";
		directories.insert(root);
		request.supplementalRoots.push_back(
			{ root, "external:" + std::to_string(index), "test" });
	}

	RuntimeResource::ResourceCatalogFileAccess fileAccess;
	fileAccess.readFileFromRoot =
		[&files, key](const fs::path& root,
			std::string_view relativePath,
			std::size_t maximumBytes)
		{
			CatalogFileReadResult result;
			const auto found = files.find(key(root, relativePath));
			if (found == files.end())
			{
				result.status = CatalogFileReadStatus::NotFound;
				return result;
			}
			if (found->second.size() > maximumBytes)
			{
				result.status = CatalogFileReadStatus::TooLarge;
				return result;
			}
			result.status = CatalogFileReadStatus::Success;
			result.bytes.assign(
				found->second.begin(), found->second.end());
			return result;
		};
	fileAccess.getDirectoryStatus =
		[&directories](const fs::path& path)
		{
			return directories.count(
				path.lexically_normal().generic_u8string()) > 0
				? CatalogDirectoryStatus::Exists
				: CatalogDirectoryStatus::Missing;
		};
	const RuntimeResource::ResourceCatalogSnapshotResult snapshot =
		RuntimeResource::loadResourceCatalogSnapshot(request, fileAccess);
	const RuntimeResource::ExactSelectionResult selected =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			request, "root", fileAccess);
	bool ok = check(
		snapshot.succeeded() &&
			hasDiagnostic(
				snapshot.snapshot.diagnostics,
				"resource.catalog.supplemental_ini_budget_reached"),
		"supplemental budget exhaustion remains an isolated snapshot warning");
	ok = check(
		selected.succeeded(),
		"supplemental discovery budget preserves exact primary selection") && ok;
	return ok;
}

bool testInvalidPrimaryDoesNotConsumeSupplementalPackSlot()
{
	using RuntimeResource::CatalogDirectoryStatus;
	using RuntimeResource::CatalogFileReadResult;
	using RuntimeResource::CatalogFileReadStatus;

	std::string index;
	const std::size_t validPrimaryCount =
		RuntimeResource::MaximumCatalogPackCount - 1;
	for (std::size_t packIndex = 0;
		packIndex < validPrimaryCount;
		++packIndex)
	{
		index += "[Pack.P" + std::to_string(packIndex) +
			"]\nPath=valid-" + std::to_string(packIndex) + "\n";
	}
	index += "[Pack.BAD]\nPath=invalid\n";

	RuntimeResource::ResourceCatalogRequest request;
	request.primaryCollectionRoot = "primary";
	request.supplementalRoots.push_back(
		{ "external/good", "external:good", "test" });

	RuntimeResource::ResourceCatalogFileAccess fileAccess;
	fileAccess.readFileFromRoot =
		[&index](const fs::path& root,
			std::string_view relativePath,
			std::size_t maximumBytes)
		{
			CatalogFileReadResult result;
			const std::string rootText =
				root.lexically_normal().generic_u8string();
			std::string content;
			if (rootText == "primary" &&
				relativePath == "resources.ini")
			{
				content = index;
			}
			else if (relativePath == "game_profile.ini" &&
				rootText.rfind("primary/valid-", 0) == 0)
			{
				content = "[Game]\nId=P" +
					rootText.substr(std::string("primary/valid-").size()) +
					"\nName=Primary\nType=0\n";
			}
			else if (rootText == "primary/invalid" &&
				relativePath == "game_profile.ini")
			{
				content = "[Game]\nName=Missing Id\nType=0\n";
			}
			else if (rootText == "external/good" &&
				relativePath == "game_profile.ini")
			{
				content =
					"[Game]\nId=EXTERNAL\nName=External\nType=3\n";
			}
			else
			{
				result.status = CatalogFileReadStatus::NotFound;
				return result;
			}
			if (content.size() > maximumBytes)
			{
				result.status = CatalogFileReadStatus::TooLarge;
				return result;
			}
			result.status = CatalogFileReadStatus::Success;
			result.bytes.assign(content.begin(), content.end());
			return result;
		};
	fileAccess.getDirectoryStatus =
		[](const fs::path& path)
		{
			const std::string pathText =
				path.lexically_normal().generic_u8string();
			return pathText == "primary" ||
				pathText.rfind("primary/", 0) == 0 ||
				pathText == "external/good"
				? CatalogDirectoryStatus::Exists
				: CatalogDirectoryStatus::Missing;
		};
	fileAccess.listChildDirectories =
		[validPrimaryCount](const fs::path& path)
		{
			RuntimeResource::CatalogDirectoryListResult result;
			if (path.lexically_normal() != fs::path("primary"))
			{
				return result;
			}
			result.status = RuntimeResource::
				CatalogDirectoryListStatus::Success;
			result.childDirectoryNames.reserve(
				validPrimaryCount + 1);
			for (std::size_t packIndex = 0;
				packIndex < validPrimaryCount;
				++packIndex)
			{
				result.childDirectoryNames.push_back(
					"valid-" + std::to_string(packIndex));
			}
			result.childDirectoryNames.push_back("invalid");
			return result;
		};

	const RuntimeResource::ResourceCatalogSnapshotResult snapshot =
		RuntimeResource::loadResourceCatalogSnapshot(request, fileAccess);
	bool ok = check(
		snapshot.succeeded() &&
			snapshot.snapshot.entries.size() ==
				RuntimeResource::MaximumCatalogPackCount &&
			findEntry(snapshot.snapshot, "external:good") != nullptr &&
			hasDiagnostic(
				snapshot.snapshot.diagnostics,
				"resource.catalog.discovered_manifest_invalid"),
		"an invalid primary manifest only skips itself and leaves the final"
		" catalog slot available to a valid supplemental pack");
	const RuntimeResource::ExactSelectionResult selected =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			request, "external:good", fileAccess);
	ok = check(
		selected.succeeded() &&
			selected.selection.activeResourceRoot ==
				fs::path("external/good"),
		"the supplemental pack remains exactly selectable at the catalog limit")
		&& ok;
	return ok;
}

bool testWritableCopyReplacesPackagedCopy(const fs::path& root)
{
	const fs::path fixtureRoot = root / "writable-override";
	bool ok = writeText(
		fixtureRoot / "primary/resources.ini",
		"[Collection]\nCommonPath=common\n") &&
		writeText(
			fixtureRoot / "primary/common/config/common.txt",
			"common\n") &&
		writeText(
			fixtureRoot / "primary/packaged/game_profile.ini",
			"[Game]\nId=YYCS\nName=Packaged\nType=1\n"
			"[UI]\nProfile=YYCS\n") &&
		writeText(
			fixtureRoot / "writable/downloaded/game_profile.ini",
			"[Game]\nId=yycs\nName=Downloaded\nType=1\n"
			"[UI]\nProfile=YYCS\n");
	if (!check(ok, "create writable override fixture"))
	{
		return false;
	}

	RuntimeResource::ResourceCatalogRequest request;
	request.primaryCollectionRoot = fixtureRoot / "primary";
	request.supplementalRoots.push_back(
		{
			fixtureRoot / "writable/downloaded",
			"application:downloaded",
			"application-resource",
			true
		});
	const RuntimeResource::ResourceCatalogSnapshotResult snapshot =
		RuntimeResource::loadResourceCatalogSnapshot(request);
	ok = check(
		snapshot.succeeded() && snapshot.snapshot.entries.size() == 1 &&
			snapshot.snapshot.entries.front().stableKey ==
				"application:downloaded" &&
			!hasDiagnostic(
				snapshot.snapshot.diagnostics,
				"resource.catalog.duplicate_game_id"),
		"one writable copy hides the packaged copy with the same Game.Id") && ok;

	const RuntimeResource::ExactSelectionResult selected =
		RuntimeResource::resolveResourceCatalogEntrySelection(
			request, "application:downloaded");
	ok = check(
		selected.succeeded() &&
			selected.selection.activeManifest.name == "Downloaded" &&
			selected.selection.activeResourceRoot ==
				resolvedPath(fixtureRoot / "writable/downloaded"),
		"the writable replacement remains exactly selectable") && ok;

	ok = writeText(
		fixtureRoot / "writable/duplicate/game_profile.ini",
		"[Game]\nId=YYCS\nName=Duplicate writable\nType=1\n"
		"[UI]\nProfile=YYCS\n") && ok;
	request.supplementalRoots.push_back(
		{
			fixtureRoot / "writable/duplicate",
			"application:duplicate",
			"application-resource",
			true
		});
	const RuntimeResource::ResourceCatalogSnapshotResult ambiguous =
		RuntimeResource::loadResourceCatalogSnapshot(request);
	ok = check(
		ambiguous.succeeded() && ambiguous.snapshot.entries.size() == 3 &&
			hasDiagnostic(
				ambiguous.snapshot.diagnostics,
				"resource.catalog.duplicate_game_id"),
		"multiple writable copies remain an explicit Game.Id conflict") && ok;
	return ok;
}
}

int main()
{
	const auto unique = std::chrono::steady_clock::now().
		time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() /
		("jxqy_resource_catalog_supplemental_" +
			std::to_string(unique));
	std::error_code error;
	fs::remove_all(root, error);
	if (!check(createFixture(root),
			"create supplemental resource catalog fixture"))
	{
		fs::remove_all(root, error);
		return 1;
	}

	bool ok = testSnapshotAggregation(root);
	ok = testCombinedSelection(root) && ok;
	ok = testPrimaryDirectChildrenAreDiscovered(root) && ok;
	ok = testPackagedRootUsesConventionalCommon() && ok;
	ok = testSupplementalBudgetDoesNotConsumeSelectionBudget() && ok;
	ok = testInvalidPrimaryDoesNotConsumeSupplementalPackSlot() && ok;
	ok = testWritableCopyReplacesPackagedCopy(root) && ok;
	fs::remove_all(root, error);
	if (ok)
	{
		std::cout <<
			"Supplemental resource catalog aggregation tests passed\n";
	}
	return ok ? 0 : 1;
}
