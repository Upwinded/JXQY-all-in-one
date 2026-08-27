#include "EditorRunResourceRouting.h"
#include "EditorRunRuntimeSession.h"

#include "../File/RootedResourceReader.h"
#include "../File/StrictRelativeResourcePath.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace
{
namespace fs = std::filesystem;

void setFailure(
	EditorRun::ResourcePreparationResult& result,
	EditorRun::ResourcePreparationError error,
	std::string diagnosticCode,
	std::string message,
	std::string fieldPath = {},
	std::string resourcePackId = {},
	std::string virtualPath = {},
	fs::path hostPath = {})
{
	result.error = error;
	result.diagnosticCode = std::move(diagnosticCode);
	result.message = std::move(message);
	result.fieldPath = std::move(fieldPath);
	result.resourcePackId = std::move(resourcePackId);
	result.virtualPath = std::move(virtualPath);
	result.hostPath = std::move(hostPath);
}

EditorRun::SearchRootKind mapContentRootKind(
	RuntimeResource::ContentRootKind kind)
{
	using ContentKind = RuntimeResource::ContentRootKind;
	switch (kind)
	{
	case ContentKind::Active:
		return EditorRun::SearchRootKind::Active;
	case ContentKind::DependencyId:
		return EditorRun::SearchRootKind::DependencyId;
	case ContentKind::Common:
		return EditorRun::SearchRootKind::Common;
	}
	return EditorRun::SearchRootKind::Active;
}

EditorRun::RuntimeTraceRootKind mapTraceContentRootKind(
	RuntimeResource::ContentRootKind kind)
{
	using ContentKind = RuntimeResource::ContentRootKind;
	switch (kind)
	{
	case ContentKind::Active:
		return EditorRun::RuntimeTraceRootKind::Active;
	case ContentKind::DependencyId:
		return EditorRun::RuntimeTraceRootKind::DependencyId;
	case ContentKind::Common:
		return EditorRun::RuntimeTraceRootKind::Common;
	}
	return EditorRun::RuntimeTraceRootKind::Active;
}

bool resourcePackIdsEqual(
	std::string_view left,
	std::string_view right)
{
	if (left.size() != right.size())
	{
		return false;
	}
	for (std::size_t index = 0;
		index < left.size();
		++index)
	{
		const unsigned char leftCharacter =
			static_cast<unsigned char>(left[index]);
		const unsigned char rightCharacter =
			static_cast<unsigned char>(right[index]);
		const unsigned char foldedLeft =
			leftCharacter >= 'A' && leftCharacter <= 'Z'
				? static_cast<unsigned char>(
					leftCharacter - 'A' + 'a')
				: leftCharacter;
		const unsigned char foldedRight =
			rightCharacter >= 'A' && rightCharacter <= 'Z'
				? static_cast<unsigned char>(
					rightCharacter - 'A' + 'a')
				: rightCharacter;
		if (foldedLeft != foldedRight)
		{
			return false;
		}
	}
	return true;
}

std::string logicalRootPathKey(const fs::path& path)
{
	std::string key =
		path.lexically_normal().generic_u8string();
#if defined(_WIN32)
	std::transform(
		key.begin(), key.end(), key.begin(),
		[](unsigned char character)
		{
			return character >= 'A' &&
					character <= 'Z'
				? static_cast<char>(
					character - 'A' + 'a')
				: static_cast<char>(character);
		});
#endif
	return key;
}

std::optional<EditorRun::TraceContentRootIdentity>
	stableTraceIdentityForOverlayOrigin(
		const RuntimeResource::ExactResourceSelection& resources,
		const EditorRun::RuntimeSessionOverlayOrigin& published)
{
	if (!published.rootKind)
	{
		return std::nullopt;
	}

	std::optional<EditorRun::TraceContentRootIdentity> matched;
	for (std::size_t ordinal = 0;
		ordinal < resources.orderedContentRoots.size();
		++ordinal)
	{
		const RuntimeResource::ContentRoot& current =
			resources.orderedContentRoots[ordinal];
		const EditorRun::RuntimeTraceRootKind currentKind =
			mapTraceContentRootKind(current.kind);
		if (currentKind != *published.rootKind)
		{
			continue;
		}

		bool stableKeyMatches = false;
		switch (currentKind)
		{
		case EditorRun::RuntimeTraceRootKind::Active:
		{
			const bool pathMatches =
				!published.rootPath.empty() &&
				logicalRootPathKey(current.root) ==
					logicalRootPathKey(
						published.rootPath);
			const bool publishedHasId =
				published.resourcePackId &&
				!published.resourcePackId->empty();
			stableKeyMatches =
				pathMatches &&
				(publishedHasId
					? resourcePackIdsEqual(
						current.resourcePackId,
						*published.resourcePackId)
					: current.resourcePackId.empty());
			break;
		}
		case EditorRun::RuntimeTraceRootKind::Common:
			stableKeyMatches =
				!published.rootPath.empty() &&
				logicalRootPathKey(current.root) ==
					logicalRootPathKey(
						published.rootPath);
			break;
		case EditorRun::RuntimeTraceRootKind::DependencyId:
			stableKeyMatches =
				published.resourcePackId &&
				!published.resourcePackId->empty() &&
				resourcePackIdsEqual(
					current.resourcePackId,
					*published.resourcePackId);
			break;
		}
		if (!stableKeyMatches || matched)
		{
			if (stableKeyMatches)
			{
				return std::nullopt;
			}
			continue;
		}

		EditorRun::TraceContentRootIdentity identity;
		identity.kind = currentKind;
		identity.ordinal =
			static_cast<std::uint64_t>(ordinal);
		if (!current.resourcePackId.empty())
		{
			identity.resourcePackId =
				current.resourcePackId;
		}
		matched = std::move(identity);
	}
	return matched;
}

std::optional<std::string> normalizedVirtualPath(
	std::string_view path)
{
	const ResourcePathSafety::StrictRelativePathResult normalized =
		ResourcePathSafety::normalizeStrictRelativeResourcePath(path);
	if (!normalized.succeeded())
	{
		return std::nullopt;
	}
	return normalized.normalizedPath;
}

std::string virtualPathComparisonKey(std::string path)
{
#if defined(_WIN32)
	std::transform(
		path.begin(),
		path.end(),
		path.begin(),
		[](unsigned char character)
		{
			return character >= 'A' && character <= 'Z'
				? static_cast<char>(
					character + ('a' - 'A'))
				: static_cast<char>(character);
		});
#endif
	return path;
}

}

namespace EditorRun
{
ResolvedSceneTarget prepareSceneTarget(const SceneTarget& target)
{
	ResolvedSceneTarget prepared;
	prepared.map =
		{
			TargetFileKind::Map,
			target.mapPath,
			SearchAllResourceRoots
		};
	const auto prepareOptional =
		[](TargetFileKind kind,
			const std::string& virtualPath,
			std::optional<ResolvedTargetFile>& output)
		{
			if (virtualPath.empty())
			{
				return;
			}
			output = ResolvedTargetFile{
				kind,
				virtualPath,
				SearchAllResourceRoots
			};
		};
	prepareOptional(
			TargetFileKind::Npc,
			target.npcPath,
			prepared.npc);
	prepareOptional(
			TargetFileKind::Object,
			target.objectPath,
			prepared.object);
	prepareOptional(
			TargetFileKind::EntryScript,
			target.entryScriptPath,
			prepared.entryScript);
	return prepared;
}

ResourcePreparationResult prepareEditorRunResources(
	const Descriptor& descriptor,
	std::string_view currentEngineVersion)
{
	(void)currentEngineVersion;
	ResourcePreparationResult result;
	const RuntimeResource::ExactSelectionResult selection =
		descriptor.activeResourcePackEntryKey.empty()
		? RuntimeResource::resolveExactResourceSelection(
			  descriptor.assetsCollectionRoot,
			  descriptor.activeResourcePackId)
		: RuntimeResource::resolveResourceCatalogEntrySelection(
			  descriptor.assetsCollectionRoot,
			  descriptor.activeResourcePackEntryKey);
	if (!selection.succeeded())
	{
		// A broken or missing catalog is an empty resource source, not a process
		// startup gate. The private overlay still lets the editor run current
		// content, and every absent formal resource remains visibly absent.
		std::error_code absoluteError;
		fs::path collectionRoot = descriptor.assetsCollectionRoot;
		if (!collectionRoot.is_absolute())
		{
			collectionRoot = fs::absolute(
				collectionRoot, absoluteError);
		}
		if (!absoluteError)
		{
			collectionRoot = collectionRoot.lexically_normal();
		}
		result.prepared.resources.assetsCollectionRoot =
			std::move(collectionRoot);
		result.prepared.resources.activeManifest =
			ResourceManifest::createDefault("");
		const std::string requestedId =
			descriptor.activeResourcePackId.empty()
				? std::string("EDITOR_RUN_EMPTY")
				: descriptor.activeResourcePackId;
		result.prepared.resources.activeManifest.id = requestedId;
		result.prepared.resources.activeManifest.name = requestedId;
		result.prepared.resources.canonicalActiveResourcePackId =
			requestedId;
		result.prepared.resources.effectiveSaveNamespace =
			"editor-run";
		result.diagnosticCode = selection.diagnosticCode;
		result.message = selection.message;
	}
	else
	{
		result.prepared.resources = selection.selection;
	}

	std::error_code overlayError;
	if (fs::is_directory(descriptor.overlayRoot, overlayError) &&
		!overlayError)
	{
		fs::path overlayRoot =
			descriptor.overlayRoot.is_absolute()
				? descriptor.overlayRoot
				: fs::absolute(
					descriptor.overlayRoot,
					overlayError);
		if (!overlayError && !overlayRoot.empty())
		{
			overlayRoot = overlayRoot.lexically_normal();
			RootedResourceReader::RootAnchorResult
				overlayAnchor =
					RootedResourceReader::openRootAnchor(
						overlayRoot);
			if (!overlayAnchor.succeeded())
			{
				setFailure(
					result,
					ResourcePreparationError::OverlayRootUnavailable,
					"editor_run.isolation.overlay_unavailable",
					"Editor-run overlay root could not be retained as one stable native directory",
					"overlayRoot",
					{},
					{},
					overlayRoot);
				return result;
			}
			result.prepared.orderedSearchRoots.push_back(
				{
					SearchRootKind::Overlay,
					overlayRoot,
					{},
					std::move(overlayAnchor.anchor)
				});
		}
	}
	for (std::size_t ordinal = 0;
		ordinal <
			result.prepared.resources.orderedContentRoots.size();
		++ordinal)
	{
		const RuntimeResource::ContentRoot& contentRoot =
			result.prepared.resources.orderedContentRoots[ordinal];
		if (contentRoot.kind ==
			RuntimeResource::ContentRootKind::Common)
		{
			continue;
		}
		SearchRoot searchRoot{
			mapContentRootKind(contentRoot.kind),
			contentRoot.root,
			contentRoot.resourcePackId,
			{}
		};
		TraceContentRootIdentity traceIdentity;
		traceIdentity.kind =
			mapTraceContentRootKind(contentRoot.kind);
		traceIdentity.ordinal =
			static_cast<std::uint64_t>(ordinal);
		if (!contentRoot.resourcePackId.empty())
		{
			traceIdentity.resourcePackId =
				contentRoot.resourcePackId;
		}
		searchRoot.traceContentRoot =
			std::move(traceIdentity);
		result.prepared.orderedSearchRoots.push_back(
			std::move(searchRoot));
	}

	result.prepared.target = prepareSceneTarget(descriptor.target);
	return result;
}

ResourcePreparationResult prepareEditorRunResources(
	const RuntimeSession& session,
	std::string_view currentEngineVersion)
{
	ResourcePreparationResult result =
		prepareEditorRunResources(
			session.descriptor,
			currentEngineVersion);
	if (!result.succeeded())
	{
		return result;
	}

	const auto failOutputIdentity =
		[&result](
			std::string message,
			fs::path path = {})
		{
			setFailure(
				result,
				ResourcePreparationError::OutputIdentityMismatch,
				"editor_run.isolation.output_generation_changed",
				std::move(message),
				"overlayRoot",
				{},
				{},
				std::move(path));
		};

	if (std::any_of(
			session.outputDirectoryIdentities.begin(),
			session.outputDirectoryIdentities.end(),
			[](const DirectoryIdentity& identity)
			{
				return !identity.valid ||
					identity.linkCount == 0;
			}))
	{
		failOutputIdentity(
			"Editor-run session did not publish complete private output identities");
		return result;
	}

	const auto overlay =
		std::find_if(
			result.prepared.orderedSearchRoots.begin(),
			result.prepared.orderedSearchRoots.end(),
			[](const SearchRoot& root)
			{
				return root.kind ==
					SearchRootKind::Overlay;
			});
	const DirectoryIdentity& expectedOverlay =
		session.outputDirectoryIdentities[
			outputDirectoryIndex(
				OutputDirectoryKind::Overlay)];
	if (overlay ==
			result.prepared.orderedSearchRoots.end() ||
		!sameDirectoryGeneration(
			overlay->anchor.identity(),
			expectedOverlay))
	{
		failOutputIdentity(
			"Editor-run overlay root changed after the runtime session was anchored",
			session.descriptor.overlayRoot);
		return result;
	}

	const auto failOverlayOrigin =
		[&result](
			std::string message,
			std::string virtualPath = {})
		{
			setFailure(
				result,
				ResourcePreparationError::OverlayOriginMismatch,
				"editor_run.resource.overlay_origin_mismatch",
				std::move(message),
				"resourceRoutingContract.traceOverlayOrigins",
				{},
				std::move(virtualPath));
		};
	std::vector<std::string> overlayOriginKeys;
	overlayOriginKeys.reserve(
		session.traceOverlayOrigins.size());
	overlay->traceOverlayOrigins.clear();
	overlay->traceOverlayOrigins.reserve(
		session.traceOverlayOrigins.size());
	for (const RuntimeSessionOverlayOrigin& publishedOrigin :
		session.traceOverlayOrigins)
	{
		const std::optional<std::string> virtualPath =
			normalizedVirtualPath(
				publishedOrigin.virtualPath);
		const std::string comparisonKey =
			virtualPath
				? virtualPathComparisonKey(*virtualPath)
				: std::string();
		if (!virtualPath ||
			std::find(
				overlayOriginKeys.cbegin(),
				overlayOriginKeys.cend(),
				comparisonKey) != overlayOriginKeys.cend())
		{
			failOverlayOrigin(
				"Editor-run overlay origin is unsafe or duplicated",
				publishedOrigin.virtualPath);
			return result;
		}
		const RootedResourceReader::ProbeResult probe =
			RootedResourceReader::probeRegularFileFromRoot(
				overlay->anchor,
				*virtualPath);
		if (!probe.succeeded())
		{
			failOverlayOrigin(
				"Editor-run overlay origin does not name a retained regular overlay file",
				publishedOrigin.virtualPath);
			return result;
		}

		TraceOverlayOrigin origin;
		origin.virtualPath = *virtualPath;
		const std::optional<TraceContentRootIdentity>
			stableIdentity =
				stableTraceIdentityForOverlayOrigin(
					result.prepared.resources,
					publishedOrigin);
		if (stableIdentity)
		{
			origin.contentRoot = *stableIdentity;
		}
		else
		{
			// Legacy ordinals and incomplete or stale logical keys never
			// choose a current content root. Trace remains usable without
			// making resource startup depend on editor-time catalog order.
			origin.contentRoot.ordinal =
				UnknownTraceContentRootOrdinal;
		}
		overlayOriginKeys.push_back(comparisonKey);
		overlay->traceOverlayOrigins.push_back(
			std::move(origin));
	}

	return result;
}
}
