#include "SavedSceneLaunchPreparation.h"

#include "EditorAssetPath.h"
#include "ProjectManager.h"

#include "../../src/File/ResourcePathSafety.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

#include <filesystem>

namespace
{
namespace fs = std::filesystem;

fs::path hostPath(const QString& path)
{
    const QByteArray utf8 = path.toUtf8();
    return fs::u8path(
        utf8.constData(),
        utf8.constData() + utf8.size());
}

QString hostPathText(const fs::path& path)
{
    if (path.empty())
        return {};

    std::error_code error;
    const fs::path absolutePath = path.is_absolute()
        ? path.lexically_normal()
        : fs::absolute(path, error).lexically_normal();
    if (error || !absolutePath.is_absolute())
        return {};

    const auto bytes = absolutePath.generic_u8string();
    QString text = QString::fromUtf8(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()));
    text.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(text);
}

QString utf8Text(const std::string& text)
{
    return QString::fromUtf8(
        text.data(),
        static_cast<int>(text.size()));
}

bool sameHostPath(
    const fs::path& left,
    const fs::path& right);

Qt::CaseSensitivity logicalPathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool sameLogicalPath(
    const QString& left,
    const QString& right)
{
    const QString normalizedLeft =
        hostPathText(hostPath(left));
    const QString normalizedRight =
        hostPathText(hostPath(right));
    return !normalizedLeft.isEmpty() &&
        !normalizedRight.isEmpty() &&
        normalizedLeft.compare(
            normalizedRight,
            logicalPathCaseSensitivity()) == 0;
}

bool logicalVirtualPathForAbsolutePath(
    const QString& root,
    const QString& path,
    QString& virtualPath)
{
    virtualPath.clear();
    const QString normalizedRoot =
        hostPathText(hostPath(root));
    const QString normalizedPath =
        hostPathText(hostPath(path));
    if (normalizedRoot.isEmpty() ||
        normalizedPath.isEmpty())
    {
        return false;
    }

    QString prefix = normalizedRoot;
    if (!prefix.endsWith(QLatin1Char('/')))
        prefix.append(QLatin1Char('/'));
    if (!normalizedPath.startsWith(
            prefix,
            logicalPathCaseSensitivity()))
    {
        return false;
    }

    const QString relative =
        normalizedPath.mid(prefix.size());
    const QByteArray relativeUtf8 = relative.toUtf8();
    const std::string relativeBytes(
        relativeUtf8.constData(),
        static_cast<std::size_t>(
            relativeUtf8.size()));
    if (relative.isEmpty() ||
        QString::fromUtf8(
            relativeUtf8.constData(),
            relativeUtf8.size()) != relative ||
        !ResourcePathSafety::isSafeVirtualResourcePath(
            relativeBytes))
    {
        return false;
    }
    virtualPath = relative;
    return true;
}

bool appendUniqueRoot(
    QStringList& roots,
    const QString& root)
{
    const QString normalized =
        hostPathText(hostPath(root));
    if (normalized.isEmpty() ||
        !QDir::isAbsolutePath(normalized))
    {
        return false;
    }
    for (const QString& existing : roots)
    {
        if (sameLogicalPath(existing, normalized))
            return true;
    }
    roots.append(normalized);
    return true;
}

bool formalRootsForSelection(
    const RuntimeResource::ExactResourceSelection& selection,
    QStringList& roots)
{
    roots.clear();
    if (!appendUniqueRoot(
        roots,
        hostPathText(selection.assetsCollectionRoot)))
    {
        return false;
    }
    for (const RuntimeResource::ContentRoot& root :
         selection.orderedContentRoots)
    {
        if (!appendUniqueRoot(
                roots,
                hostPathText(root.root)))
        {
            return false;
        }
    }
    for (const fs::path& root :
         selection.orderedUiFallbackRoots)
    {
        if (!root.empty())
        {
            if (!appendUniqueRoot(
                    roots,
                    hostPathText(root)))
            {
                return false;
            }
        }
    }
    if (!selection.commonResourceRoot.empty())
    {
        const auto matchingContentRoot =
            std::find_if(
                selection.orderedContentRoots.cbegin(),
                selection.orderedContentRoots.cend(),
                [&selection](
                    const RuntimeResource::ContentRoot& root)
                {
                    return sameHostPath(
                        root.root,
                        selection.commonResourceRoot);
                });
        if (matchingContentRoot ==
                selection.orderedContentRoots.cend() ||
            !appendUniqueRoot(
                roots,
                hostPathText(
                    selection.commonResourceRoot)))
        {
            return false;
        }
    }
    return !roots.isEmpty();
}

ResourceContentRoot::Kind editorRootKind(
    RuntimeResource::ContentRootKind kind)
{
    switch (kind)
    {
    case RuntimeResource::ContentRootKind::Active:
        return ResourceContentRoot::Kind::Local;
    case RuntimeResource::ContentRootKind::DependencyId:
        return ResourceContentRoot::Kind::DependencyId;
    case RuntimeResource::ContentRootKind::Common:
        return ResourceContentRoot::Kind::Common;
    }
    return ResourceContentRoot::Kind::DependencyId;
}

QList<ResourceContentRoot> editorContentRoots(
    const RuntimeResource::ExactResourceSelection& selection)
{
    QList<ResourceContentRoot> roots;
    roots.reserve(
        static_cast<qsizetype>(
            selection.orderedContentRoots.size()));
    for (const RuntimeResource::ContentRoot& source :
         selection.orderedContentRoots)
    {
        ResourceContentRoot root;
        root.rootPath = hostPathText(source.root);
        root.id = utf8Text(source.resourcePackId);
        if (source.kind ==
            RuntimeResource::ContentRootKind::Active)
        {
            root.name =
                utf8Text(selection.activeManifest.name);
        }
        root.kind = editorRootKind(source.kind);
        root.available = true;
        roots.append(std::move(root));
    }
    return roots;
}

bool sameHostPath(
    const fs::path& left,
    const fs::path& right)
{
    if (left.empty() || right.empty())
        return left.empty() && right.empty();
    return sameLogicalPath(
        hostPathText(left),
        hostPathText(right));
}

bool resolveReference(
    SavedSceneReferenceKind kind,
    const QString& fieldName,
    const QString& virtualPath,
    ProjectDocumentType expectedDocumentType,
    const QList<ResourceContentRoot>& roots,
    SavedSceneResolvedReference& reference,
    SavedSceneLaunchPreparationIssue& issue)
{
    const QByteArray virtualPathUtf8 = virtualPath.toUtf8();
    const std::string pathBytes(
        virtualPathUtf8.constData(),
        static_cast<std::size_t>(virtualPathUtf8.size()));
    if (QString::fromUtf8(
            virtualPathUtf8.constData(),
            virtualPathUtf8.size()) != virtualPath ||
        !ResourcePathSafety::isSafeVirtualResourcePath(pathBytes))
    {
        issue = {
            SavedSceneLaunchPreparationError::UnsafeReferencePath,
            fieldName,
            virtualPath,
            {}
        };
        return false;
    }

    QString fallbackRoot;
    QString fallbackAbsolutePath;
    for (const ResourceContentRoot& root : roots)
    {
        if (!root.available)
            continue;

        const fs::path rootPath = hostPath(root.rootPath);
        const QString absolutePath = hostPathText(
            rootPath / fs::u8path(pathBytes));
        if (absolutePath.isEmpty())
            continue;

        if (fallbackAbsolutePath.isEmpty())
        {
            fallbackRoot = root.rootPath;
            fallbackAbsolutePath = absolutePath;
        }

        // This lookup only preserves the runtime's first-existing-root order
        // for editor buffers and diagnostics. Failure is deliberately ignored:
        // missing or unusable formal content is handled by the game as an
        // empty resource and never blocks editor-run preparation.
        if (!QFileInfo(absolutePath).isFile())
            continue;

        reference.kind = kind;
        reference.fieldName = fieldName;
        reference.virtualPath = virtualPath;
        reference.resolvedRoot = root.rootPath;
        reference.absolutePath = absolutePath;
        reference.expectedDocumentType = expectedDocumentType;
        return true;
    }

    // Keep a stable lexical route even when no formal file currently exists.
    // The game process remains authoritative and will treat the missing file
    // as an empty resource.
    reference.kind = kind;
    reference.fieldName = fieldName;
    reference.virtualPath = virtualPath;
    reference.resolvedRoot = fallbackRoot;
    reference.absolutePath = fallbackAbsolutePath;
    reference.expectedDocumentType = expectedDocumentType;
    return true;
}

bool documentAlreadyCollected(
    const QList<ProjectDocumentState>& documents,
    const QString& path)
{
    for (const ProjectDocumentState& document : documents)
    {
        if (sameLogicalPath(
                document.filePath,
                path))
            return true;
    }
    return false;
}

std::optional<ProjectDocumentState> documentForLogicalPath(
    const ProjectDocumentRegistry& registry,
    const QString& path)
{
    for (const ProjectDocumentState& document :
         registry.documents())
    {
        if (sameLogicalPath(
                document.filePath,
                path))
        {
            return document;
        }
    }
    return std::nullopt;
}

QList<const DesktopRunDocumentSnapshot*> snapshotsForPath(
    const QList<DesktopRunDocumentSnapshot>& snapshots,
    const QString& path)
{
    QList<const DesktopRunDocumentSnapshot*> matches;
    for (const DesktopRunDocumentSnapshot& snapshot : snapshots)
    {
        if (!snapshot.filePath.trimmed().isEmpty() &&
            sameLogicalPath(
                snapshot.filePath,
                path))
        {
            matches.append(&snapshot);
        }
    }
    return matches;
}

bool sameVirtualPath(
    const QString& left,
    const QString& right,
    const QString&)
{
    return left.compare(
               right,
               logicalPathCaseSensitivity()) == 0;
}

std::optional<std::size_t> contentRootOrdinalForPath(
    const QString& path,
    const QString& virtualPath,
    const QList<ResourceContentRoot>& roots)
{
    const QString normalizedPath =
        hostPathText(hostPath(path));
    if (normalizedPath.isEmpty() ||
        virtualPath.isEmpty())
        return std::nullopt;

    for (qsizetype index = 0; index < roots.size(); ++index)
    {
        const ResourceContentRoot& root = roots.at(index);
        QString candidateVirtualPath;
        if (root.available &&
            logicalVirtualPathForAbsolutePath(
                root.rootPath,
                normalizedPath,
                candidateVirtualPath) &&
            sameVirtualPath(
                candidateVirtualPath,
                virtualPath,
                root.rootPath))
        {
            return static_cast<std::size_t>(index);
        }
    }
    return std::nullopt;
}

bool appendOverlayFile(
    PreparedSavedSceneLaunch& launch,
    const QString& virtualPath,
    const DesktopRunDocumentSnapshot& snapshot,
    quint64& totalOverlayBytes,
    SavedSceneLaunchPreparationResult& result)
{
    const QByteArray virtualPathUtf8 = virtualPath.toUtf8();
    const std::string pathBytes(
        virtualPathUtf8.constData(),
        static_cast<std::size_t>(
            virtualPathUtf8.size()));
    if (virtualPath.isEmpty() ||
        QString::fromUtf8(
            virtualPathUtf8.constData(),
            virtualPathUtf8.size()) != virtualPath ||
        !ResourcePathSafety::isSafeVirtualResourcePath(
            pathBytes))
    {
        result.issues.append({
            SavedSceneLaunchPreparationError::
                DirtyDocumentSnapshotInvalid,
            QStringLiteral("overlay"),
            virtualPath,
            snapshot.filePath,
            RuntimeResource::ExactSelectionError::None,
            QStringLiteral(
                "editor_run.overlay.invalid_virtual_path")
        });
        return false;
    }

    std::optional<std::size_t> contentRootOrdinal;
    if (!snapshot.overlayVirtualPath.trimmed().isEmpty())
    {
        for (qsizetype index = 0;
             index < launch.orderedContentRoots.size();
             ++index)
        {
            const ResourceContentRoot& root =
                launch.orderedContentRoots.at(index);
            if (root.available &&
                sameLogicalPath(
                    root.rootPath,
                    launch.activeContentRoot))
            {
                contentRootOrdinal =
                    static_cast<std::size_t>(index);
                break;
            }
        }
    }
    else
    {
        contentRootOrdinal =
            contentRootOrdinalForPath(
                snapshot.filePath,
                virtualPath,
                launch.orderedContentRoots);
    }
    if (!contentRootOrdinal.has_value())
    {
        result.issues.append({
            SavedSceneLaunchPreparationError::
                DirtyDocumentSnapshotInvalid,
            QStringLiteral("overlay"),
            virtualPath,
            snapshot.filePath,
            RuntimeResource::ExactSelectionError::None,
            QStringLiteral(
                "editor_run.overlay.snapshot_outside_content_roots")
        });
        return false;
    }

    for (const PreparedDesktopRunOverlayFile& existing :
         launch.overlayFiles)
    {
        if (!sameVirtualPath(
                existing.virtualPath,
                virtualPath,
                launch.activeContentRoot))
        {
            continue;
        }
        if (sameLogicalPath(
                existing.sourcePath,
                snapshot.filePath) &&
            existing.documentType == snapshot.type &&
            existing.bytes == snapshot.bytes)
        {
            return true;
        }
        result.issues.append({
            SavedSceneLaunchPreparationError::
                OverlayPathCollision,
            QStringLiteral("overlay"),
            virtualPath,
            snapshot.filePath,
            RuntimeResource::ExactSelectionError::None,
            QStringLiteral(
                "editor_run.overlay.path_collision")
        });
        return false;
    }

    const quint64 byteCount =
        static_cast<quint64>(snapshot.bytes.size());
    if (launch.overlayFiles.size() >=
            MaximumDesktopRunOverlayFileCount ||
        byteCount > MaximumDesktopRunOverlayBytes ||
        totalOverlayBytes >
            MaximumDesktopRunOverlayBytes - byteCount)
    {
        result.issues.append({
            SavedSceneLaunchPreparationError::
                OverlayLimitExceeded,
            QStringLiteral("overlay"),
            virtualPath,
            snapshot.filePath,
            RuntimeResource::ExactSelectionError::None,
            QStringLiteral(
                "editor_run.overlay.limit_exceeded")
        });
        return false;
    }

    PreparedDesktopRunOverlayFile file;
    file.virtualPath = virtualPath;
    file.sourcePath =
        hostPathText(hostPath(snapshot.filePath));
    file.contentRootOrdinal = *contentRootOrdinal;
    file.documentType = snapshot.type;
    file.bytes = snapshot.bytes;
    file.sha256 = QCryptographicHash::hash(
        file.bytes,
        QCryptographicHash::Sha256);
    launch.overlayFiles.append(std::move(file));
    totalOverlayBytes += byteCount;
    return true;
}

bool virtualPathForSnapshot(
    const DesktopRunDocumentSnapshot& snapshot,
    const QList<ResourceContentRoot>& roots,
    QString& virtualPath)
{
    if (!snapshot.overlayVirtualPath.trimmed().isEmpty())
    {
        QString normalized;
        if (!EditorAssetPath::normalizeResourcePath(
                snapshot.overlayVirtualPath,
                normalized))
        {
            return false;
        }
        virtualPath = normalized;
        return true;
    }

    const QString normalizedPath =
        hostPathText(hostPath(snapshot.filePath));
    if (normalizedPath.isEmpty())
        return false;
    for (const ResourceContentRoot& root : roots)
    {
        if (!root.available)
        {
            continue;
        }
        if (logicalVirtualPathForAbsolutePath(
                root.rootPath,
                normalizedPath,
                virtualPath))
        {
            return true;
        }
    }
    return false;
}
}

static SavedSceneLaunchPreparationResult prepareSceneLaunch(
    const ProjectManager& projectManager,
    const ProjectDocumentRegistry& documentRegistry,
    const ProjectScene& selectedScene,
    EditorRun::TargetKind targetKind,
    const QList<DesktopRunDocumentSnapshot>& documentSnapshots)
{
    SavedSceneLaunchPreparationResult result;
    const QByteArray requestedIdUtf8 =
        projectManager.activeResourcePackId().toUtf8();
    const std::string requestedId(
        requestedIdUtf8.constData(),
        static_cast<std::size_t>(
            requestedIdUtf8.size()));
    const QByteArray requestedEntryKeyUtf8 =
        projectManager.activeResourcePackEntryKey().toUtf8();
    const std::string requestedEntryKey(
        requestedEntryKeyUtf8.constData(),
        static_cast<std::size_t>(
            requestedEntryKeyUtf8.size()));
    const RuntimeResource::ExactSelectionResult selectionResult =
        requestedEntryKey.empty()
        ? RuntimeResource::resolveExactResourceSelection(
              hostPath(projectManager.editableAssetsRoot()),
              requestedId)
        : RuntimeResource::resolveResourceCatalogEntrySelection(
              hostPath(projectManager.editableAssetsRoot()),
              requestedEntryKey);
    PreparedSavedSceneLaunch candidate;
    candidate.targetKind = targetKind;
    candidate.scene = selectedScene;
    if (selectionResult.succeeded())
    {
        const RuntimeResource::ExactResourceSelection& selection =
            selectionResult.selection;
        candidate.assetsCollectionRoot =
            hostPathText(selection.assetsCollectionRoot);
        candidate.canonicalActiveResourcePackId =
            utf8Text(
                selection.canonicalActiveResourcePackId);
        candidate.activeResourcePackEntryKey =
            utf8Text(selection.stableActiveEntryKey);
        candidate.activeContentRoot =
            hostPathText(selection.activeResourceRoot);
        candidate.orderedContentRoots =
            editorContentRoots(selection);
        if (!formalRootsForSelection(
                selection,
                candidate.formalRoots))
        {
            result.issues.append({
                SavedSceneLaunchPreparationError::
                    ResourceContextUnavailable,
                QStringLiteral("resources"),
                {},
                {},
                RuntimeResource::ExactSelectionError::None,
                QStringLiteral(
                    "editor_run.resource.routing_path_invalid")
            });
            return result;
        }
    }
    else
    {
        const QString authoringRoot = hostPathText(
            hostPath(projectManager.editableAssetsRoot()));
        if (authoringRoot.isEmpty() ||
            !appendUniqueRoot(
                candidate.formalRoots,
                authoringRoot))
        {
            result.issues.append({
                SavedSceneLaunchPreparationError::
                    ResourceContextUnavailable,
                QStringLiteral("resources"),
                {},
                projectManager.editableAssetsRoot(),
                selectionResult.error,
                QStringLiteral(
                    "editor_run.resource.routing_path_invalid")
            });
            return result;
        }

        candidate.assetsCollectionRoot = authoringRoot;
        candidate.canonicalActiveResourcePackId =
            projectManager.activeResourcePackId().trimmed();
        if (candidate.canonicalActiveResourcePackId.isEmpty())
        {
            candidate.canonicalActiveResourcePackId =
                QStringLiteral("EDITOR_RUN_EMPTY");
        }
        candidate.activeResourcePackEntryKey =
            projectManager.activeResourcePackEntryKey().trimmed();
        candidate.activeContentRoot = authoringRoot;
        ResourceContentRoot authoringContentRoot;
        authoringContentRoot.rootPath = authoringRoot;
        authoringContentRoot.id =
            candidate.canonicalActiveResourcePackId;
        authoringContentRoot.kind =
            ResourceContentRoot::Kind::Local;
        authoringContentRoot.available = true;
        candidate.orderedContentRoots.append(
            std::move(authoringContentRoot));
    }

    const DesktopRunDocumentSnapshot*
        explicitMapAuthority = nullptr;
    const DesktopRunDocumentSnapshot*
        explicitNpcAuthority = nullptr;
    const DesktopRunDocumentSnapshot*
        explicitObjectAuthority = nullptr;
    const DesktopRunDocumentSnapshot*
        explicitScriptAuthority = nullptr;
    auto selectExplicitAuthority =
        [&candidate, &documentSnapshots, &result](
            ProjectDocumentType documentType,
            const QString& fieldName,
            const QString& expectedVirtualPath,
            const QString& missingDiagnosticCode,
            const QString& ambiguousDiagnosticCode,
            const QString& invalidDiagnosticCode,
            const DesktopRunDocumentSnapshot*& authority)
        {
            QList<const DesktopRunDocumentSnapshot*> candidates;
            for (const DesktopRunDocumentSnapshot& snapshot :
                 documentSnapshots)
            {
                if (snapshot.type == documentType &&
                    snapshot.includeInOverlay)
                {
                    candidates.append(&snapshot);
                }
            }

            if (candidates.size() != 1)
            {
                const bool missing = candidates.isEmpty();
                result.issues.append({
                    missing
                        ? SavedSceneLaunchPreparationError::
                              DirtyDocumentSnapshotUnavailable
                        : SavedSceneLaunchPreparationError::
                              DirtyDocumentSnapshotInvalid,
                    fieldName,
                    expectedVirtualPath,
                    {},
                    RuntimeResource::ExactSelectionError::None,
                    missing
                        ? missingDiagnosticCode
                        : ambiguousDiagnosticCode
                });
                return false;
            }

            const DesktopRunDocumentSnapshot* snapshotCandidate =
                candidates.constFirst();
            QString virtualPath;
            bool valid =
                snapshotCandidate->serializationSupported &&
                virtualPathForSnapshot(
                    *snapshotCandidate,
                    candidate.orderedContentRoots,
                    virtualPath);
            if (valid)
            {
                valid =
                    sameVirtualPath(
                        virtualPath,
                        expectedVirtualPath,
                        candidate.activeContentRoot);
            }
            if (!valid)
            {
                result.issues.append({
                    SavedSceneLaunchPreparationError::
                        DirtyDocumentSnapshotInvalid,
                    fieldName,
                    expectedVirtualPath,
                    snapshotCandidate->filePath,
                    RuntimeResource::ExactSelectionError::None,
                    invalidDiagnosticCode
                });
                return false;
            }
            authority = snapshotCandidate;
            return true;
        };
    auto selectOptionalExplicitAuthority =
        [&candidate, &documentSnapshots, &result](
            ProjectDocumentType documentType,
            const QString& fieldName,
            const QString& expectedVirtualPath,
            const QString& ambiguousDiagnosticCode,
            const QString& invalidDiagnosticCode,
            const DesktopRunDocumentSnapshot*& authority)
        {
            QList<const DesktopRunDocumentSnapshot*> candidates;
            for (const DesktopRunDocumentSnapshot& snapshot :
                 documentSnapshots)
            {
                if (snapshot.type != documentType ||
                    !snapshot.includeInOverlay)
                {
                    continue;
                }
                QString virtualPath;
                if (virtualPathForSnapshot(
                        snapshot,
                        candidate.orderedContentRoots,
                        virtualPath) &&
                    sameVirtualPath(
                        virtualPath,
                        expectedVirtualPath,
                        candidate.activeContentRoot))
                {
                    candidates.append(&snapshot);
                }
            }

            if (candidates.isEmpty())
                return true;
            if (candidates.size() != 1)
            {
                result.issues.append({
                    SavedSceneLaunchPreparationError::
                        DirtyDocumentSnapshotInvalid,
                    fieldName,
                    expectedVirtualPath,
                    {},
                    RuntimeResource::ExactSelectionError::None,
                    ambiguousDiagnosticCode
                });
                return false;
            }
            if (!candidates.constFirst()->
                    serializationSupported)
            {
                result.issues.append({
                    SavedSceneLaunchPreparationError::
                        DirtyDocumentSnapshotInvalid,
                    fieldName,
                    expectedVirtualPath,
                    candidates.constFirst()->filePath,
                    RuntimeResource::ExactSelectionError::None,
                    invalidDiagnosticCode
                });
                return false;
            }
            authority = candidates.constFirst();
            return true;
        };

    if (targetKind == EditorRun::TargetKind::Map &&
        !selectExplicitAuthority(
            ProjectDocumentType::Map,
            QStringLiteral("map"),
            selectedScene.mapPath,
            QStringLiteral(
                "editor_run.current_map.snapshot_missing"),
            QStringLiteral(
                "editor_run.current_map.snapshot_ambiguous"),
            QStringLiteral(
                "editor_run.current_map.snapshot_invalid"),
            explicitMapAuthority))
    {
        return result;
    }
    if (targetKind == EditorRun::TargetKind::Map &&
        !selectedScene.npcPath.trimmed().isEmpty() &&
        !selectOptionalExplicitAuthority(
            ProjectDocumentType::NpcList,
            QStringLiteral("npc"),
            selectedScene.npcPath,
            QStringLiteral(
                "editor_run.current_map.npc_snapshot_ambiguous"),
            QStringLiteral(
                "editor_run.current_map.npc_snapshot_invalid"),
            explicitNpcAuthority))
    {
        return result;
    }
    if (targetKind == EditorRun::TargetKind::Map &&
        !selectedScene.objectPath.trimmed().isEmpty() &&
        !selectOptionalExplicitAuthority(
            ProjectDocumentType::ObjectList,
            QStringLiteral("object"),
            selectedScene.objectPath,
            QStringLiteral(
                "editor_run.current_map.object_snapshot_ambiguous"),
            QStringLiteral(
                "editor_run.current_map.object_snapshot_invalid"),
            explicitObjectAuthority))
    {
        return result;
    }
    if (targetKind == EditorRun::TargetKind::Script &&
        !selectExplicitAuthority(
            ProjectDocumentType::Script,
            QStringLiteral("entryScript"),
            selectedScene.entryScriptPath,
            QStringLiteral(
                "editor_run.current_script.snapshot_missing"),
            QStringLiteral(
                "editor_run.current_script.snapshot_ambiguous"),
            QStringLiteral(
                "editor_run.current_script.snapshot_invalid"),
            explicitScriptAuthority))
    {
        return result;
    }

    struct ReferenceInput
    {
        SavedSceneReferenceKind kind;
        const char* fieldName;
        QString path;
        ProjectDocumentType documentType;
        bool required;
    };
    const ReferenceInput inputs[] = {
        {
            SavedSceneReferenceKind::Map,
            "map",
            selectedScene.mapPath,
            ProjectDocumentType::Map,
            true
        },
        {
            SavedSceneReferenceKind::Npc,
            "npc",
            selectedScene.npcPath,
            ProjectDocumentType::NpcList,
            false
        },
        {
            SavedSceneReferenceKind::Object,
            "object",
            selectedScene.objectPath,
            ProjectDocumentType::ObjectList,
            false
        },
        {
            SavedSceneReferenceKind::EntryScript,
            "entryScript",
            selectedScene.entryScriptPath,
            ProjectDocumentType::Script,
            false
        }
    };
    for (const ReferenceInput& input : inputs)
    {
        if (!input.required && input.path.isEmpty())
            continue;
        SavedSceneResolvedReference reference;
        const DesktopRunDocumentSnapshot* authority = nullptr;
        switch (input.kind)
        {
        case SavedSceneReferenceKind::Map:
            authority = explicitMapAuthority;
            break;
        case SavedSceneReferenceKind::Npc:
            authority = explicitNpcAuthority;
            break;
        case SavedSceneReferenceKind::Object:
            authority = explicitObjectAuthority;
            break;
        case SavedSceneReferenceKind::EntryScript:
            authority = explicitScriptAuthority;
            break;
        }
        if (authority)
        {
            reference.kind = input.kind;
            reference.fieldName =
                QString::fromLatin1(input.fieldName);
            reference.virtualPath = input.path;
            reference.absolutePath =
                hostPathText(hostPath(authority->filePath));
            reference.resolvedRoot =
                candidate.activeContentRoot;
            reference.expectedDocumentType =
                input.documentType;
            if (input.kind ==
                SavedSceneReferenceKind::EntryScript)
            {
                reference.launchVerifiedBytes =
                    authority->bytes;
                reference.launchSha256 =
                    QCryptographicHash::hash(
                        reference.launchVerifiedBytes,
                        QCryptographicHash::Sha256);
                reference.launchContentFromOverlay = true;
                reference.launchSourceFromEditorBuffer = true;
            }
            candidate.references.append(
                std::move(reference));
            continue;
        }
        SavedSceneLaunchPreparationIssue issue;
        if (!resolveReference(
                input.kind,
                QString::fromLatin1(input.fieldName),
                input.path,
                input.documentType,
                candidate.orderedContentRoots,
                reference,
                issue))
        {
            result.issues.append(issue);
            continue;
        }
        candidate.references.append(reference);
    }
    if (!result.issues.isEmpty())
        return result;

    QString selectedMapAbsolutePath;
    for (const SavedSceneResolvedReference& reference :
         candidate.references)
    {
        if (reference.kind == SavedSceneReferenceKind::Map)
        {
            selectedMapAbsolutePath = reference.absolutePath;
            break;
        }
    }

    const QString pendingMpcOwnerMapPath =
        selectedMapAbsolutePath;
    const bool allowUnboundCurrentMapSupplement =
        targetKind == EditorRun::TargetKind::Map &&
        explicitMapAuthority &&
        explicitMapAuthority->filePath.trimmed().isEmpty();

    quint64 totalOverlayBytes = 0;
    for (SavedSceneResolvedReference& reference :
         candidate.references)
    {
        const std::optional<ProjectDocumentState>
            document =
                documentForLogicalPath(
                    documentRegistry,
                    reference.absolutePath);
        if (!document)
            continue;
        if (document->type != reference.expectedDocumentType)
        {
            result.issues.append({
                SavedSceneLaunchPreparationError::
                    DocumentTypeMismatch,
                reference.fieldName,
                reference.virtualPath,
                reference.absolutePath
            });
            continue;
        }
        const QList<const DesktopRunDocumentSnapshot*>
            matchingSnapshots =
                snapshotsForPath(
                    documentSnapshots,
                    document->filePath);
        bool explicitOverlayBinding = false;
        for (const DesktopRunDocumentSnapshot* snapshot :
             matchingSnapshots)
        {
            explicitOverlayBinding =
                explicitOverlayBinding ||
                snapshot->includeInOverlay;
        }
        if (!document->dirty &&
            !explicitOverlayBinding)
        {
            continue;
        }
        if (matchingSnapshots.isEmpty())
        {
            if (document->dirty &&
                !documentAlreadyCollected(
                    result.dirtyDocuments,
                    document->filePath))
            {
                result.dirtyDocuments.append(*document);
            }
            continue;
        }
        if (matchingSnapshots.size() != 1 ||
            matchingSnapshots.constFirst()->type !=
                document->type ||
            (!matchingSnapshots.constFirst()->dirty &&
             !matchingSnapshots.constFirst()->
                 includeInOverlay) ||
            !matchingSnapshots.constFirst()->
                serializationSupported)
        {
            if (!documentAlreadyCollected(
                    result.dirtyDocuments,
                    document->filePath))
            {
                result.dirtyDocuments.append(*document);
            }
            result.issues.append({
                matchingSnapshots.size() == 1 &&
                        matchingSnapshots.constFirst()->type ==
                            document->type &&
                        (matchingSnapshots.constFirst()->dirty ||
                         matchingSnapshots.constFirst()->
                             includeInOverlay)
                    ? SavedSceneLaunchPreparationError::
                          DirtyDocumentSnapshotUnavailable
                    : SavedSceneLaunchPreparationError::
                          DirtyDocumentSnapshotInvalid,
                reference.fieldName,
                reference.virtualPath,
                reference.absolutePath,
                RuntimeResource::ExactSelectionError::None,
                matchingSnapshots.size() == 1
                    ? matchingSnapshots.constFirst()->
                          diagnosticCode
                    : QStringLiteral(
                          "editor_run.overlay.snapshot_ambiguous")
            });
            continue;
        }

        const DesktopRunDocumentSnapshot& snapshot =
            *matchingSnapshots.constFirst();
        if (!appendOverlayFile(
                candidate,
                reference.virtualPath,
                snapshot,
                totalOverlayBytes,
                result))
        {
            continue;
        }
        if (reference.kind ==
            SavedSceneReferenceKind::EntryScript)
        {
            reference.launchVerifiedBytes =
                snapshot.bytes;
            reference.launchSha256 =
                QCryptographicHash::hash(
                    reference.launchVerifiedBytes,
                    QCryptographicHash::Sha256);
            reference.launchContentFromOverlay = true;
            reference.launchSourceFromEditorBuffer = true;
        }
    }

    for (const DesktopRunDocumentSnapshot& snapshot :
         documentSnapshots)
    {
        if (!snapshot.includeInOverlay)
        {
            continue;
        }
        if (snapshot.type == ProjectDocumentType::Image)
        {
            // Pending MPC payloads are supplemental to one captured MAP
            // buffer. Match their provenance to the direct MAP reference
            // resolved from the selection used by this preparation call.
            const bool belongsToSavedMap =
                !pendingMpcOwnerMapPath.isEmpty() &&
                !snapshot.ownerMapFilePath.trimmed().isEmpty() &&
                sameLogicalPath(
                    snapshot.ownerMapFilePath,
                    pendingMpcOwnerMapPath);
            const bool belongsToUnsavedCurrentMap =
                allowUnboundCurrentMapSupplement &&
                snapshot.ownerMapFilePath.trimmed().isEmpty();
            if (!belongsToSavedMap &&
                !belongsToUnsavedCurrentMap)
            {
                continue;
            }
        }
        if (!snapshot.serializationSupported ||
            (snapshot.filePath.trimmed().isEmpty() &&
             snapshot.overlayVirtualPath.trimmed().isEmpty()))
        {
            result.issues.append({
                SavedSceneLaunchPreparationError::
                    DirtyDocumentSnapshotUnavailable,
                QStringLiteral("overlay"),
                {},
                snapshot.filePath,
                RuntimeResource::ExactSelectionError::None,
                snapshot.diagnosticCode
            });
            continue;
        }
        QString virtualPath;
        if (!virtualPathForSnapshot(
                snapshot,
                candidate.orderedContentRoots,
                virtualPath))
        {
            result.issues.append({
                SavedSceneLaunchPreparationError::
                    DirtyDocumentSnapshotInvalid,
                QStringLiteral("overlay"),
                {},
                snapshot.filePath,
                RuntimeResource::ExactSelectionError::None,
                QStringLiteral(
                    "editor_run.overlay.snapshot_outside_content_roots")
            });
            continue;
        }
        appendOverlayFile(
            candidate,
            virtualPath,
            snapshot,
            totalOverlayBytes,
            result);
    }

    if (result.issues.isEmpty() &&
        result.dirtyDocuments.isEmpty())
    {
        result.prepared = std::move(candidate);
    }
    return result;
}

SavedSceneLaunchPreparationResult prepareSavedSceneLaunch(
    const ProjectManager& projectManager,
    const ProjectDocumentRegistry& documentRegistry,
    QStringView sceneId,
    const QList<DesktopRunDocumentSnapshot>& documentSnapshots)
{
    SavedSceneLaunchPreparationResult result;
    if (!projectManager.isProjectOpen())
    {
        result.issues.append({
            SavedSceneLaunchPreparationError::ProjectNotOpen,
            {},
            {},
            {}
        });
        return result;
    }
    if (projectManager.runtimeConfigurationNeedsSave())
    {
        result.issues.append({
            SavedSceneLaunchPreparationError::
                RuntimeConfigurationNeedsSave,
            {},
            {},
            {}
        });
        return result;
    }

    const ProjectRuntimeConfiguration configuration =
        projectManager.runtimeConfiguration();
    for (const ProjectScene& scene : configuration.scenes)
    {
        if (scene.id == sceneId)
        {
            return prepareSceneLaunch(
                projectManager,
                documentRegistry,
                scene,
                EditorRun::TargetKind::Scene,
                documentSnapshots);
        }
    }

    result.issues.append({
        SavedSceneLaunchPreparationError::SceneNotFound,
        QStringLiteral("sceneId"),
        sceneId.toString(),
        {}
    });
    return result;
}

SavedSceneLaunchPreparationResult prepareTransientSceneLaunch(
    const ProjectManager& projectManager,
    const ProjectDocumentRegistry& documentRegistry,
    const ProjectScene& transientScene,
    EditorRun::TargetKind targetKind,
    const QList<DesktopRunDocumentSnapshot>& documentSnapshots)
{
    SavedSceneLaunchPreparationResult result;
    if (!projectManager.isProjectOpen())
    {
        result.issues.append({
            SavedSceneLaunchPreparationError::ProjectNotOpen,
            {},
            {},
            {}
        });
        return result;
    }
    if (projectManager.runtimeConfigurationNeedsSave())
    {
        result.issues.append({
            SavedSceneLaunchPreparationError::
                RuntimeConfigurationNeedsSave,
            {},
            {},
            {}
        });
        return result;
    }
    const bool targetKindValid =
        targetKind == EditorRun::TargetKind::Map ||
        targetKind == EditorRun::TargetKind::Script;
    const bool targetPayloadValid =
        (targetKind == EditorRun::TargetKind::Map &&
         transientScene.entryScriptPath.trimmed().isEmpty()) ||
        (targetKind == EditorRun::TargetKind::Script &&
         !transientScene.entryScriptPath.trimmed().isEmpty());
    if (!targetKindValid ||
        !targetPayloadValid ||
        transientScene.id.trimmed().isEmpty() ||
        transientScene.name.trimmed().isEmpty() ||
        transientScene.mapPath.trimmed().isEmpty())
    {
        result.issues.append({
            SavedSceneLaunchPreparationError::SceneNotFound,
            QStringLiteral("target"),
            transientScene.id,
            {},
            RuntimeResource::ExactSelectionError::None,
            QStringLiteral(
                "editor_run.target.transient_invalid")
        });
        return result;
    }
    return prepareSceneLaunch(
        projectManager,
        documentRegistry,
        transientScene,
        targetKind,
        documentSnapshots);
}
