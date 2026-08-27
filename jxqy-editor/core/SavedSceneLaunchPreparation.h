#pragma once

#include "GameProfile.h"
#include "DesktopRunDocumentSnapshot.h"
#include "ProjectDocumentRegistry.h"
#include "ProjectRuntimeConfiguration.h"

#include "../../src/Launch/EditorRunDescriptor.h"
#include "../../src/Resource/ResourceCatalog.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <cstddef>
#include <optional>

class ProjectManager;

constexpr qsizetype MaximumDesktopRunOverlayFileCount = 4096;
constexpr quint64 MaximumDesktopRunOverlayBytes =
    512ULL * 1024ULL * 1024ULL;

enum class SavedSceneReferenceKind
{
    Map,
    Npc,
    Object,
    EntryScript
};

struct SavedSceneResolvedReference
{
    SavedSceneReferenceKind kind = SavedSceneReferenceKind::Map;
    QString fieldName;
    QString virtualPath;
    QString absolutePath;
    QString resolvedRoot;
    ProjectDocumentType expectedDocumentType =
        ProjectDocumentType::Map;
    // Populated only for an exact editor-buffer overlay capture. Formal
    // resources remain path-routed and do not retain launch-time content.
    QByteArray launchVerifiedBytes;
    QByteArray launchSha256;
    bool launchContentFromOverlay = false;
    bool launchSourceFromEditorBuffer = false;
};

struct PreparedDesktopRunOverlayFile
{
    QString virtualPath;
    QString sourcePath;
    // Zero-based index in PreparedSavedSceneLaunch::orderedContentRoots.
    // Overlay is a source layer, not a portable logical content root.
    std::size_t contentRootOrdinal = 0;
    ProjectDocumentType documentType =
        ProjectDocumentType::Script;
    QByteArray bytes;
    QByteArray sha256;
};

struct PreparedSavedSceneLaunch
{
    EditorRun::TargetKind targetKind =
        EditorRun::TargetKind::Scene;
    ProjectScene scene;
    QString assetsCollectionRoot;
    QString canonicalActiveResourcePackId;
    QString activeResourcePackEntryKey;
    QString activeContentRoot;
    QList<ResourceContentRoot> orderedContentRoots;
    QList<SavedSceneResolvedReference> references;
    QList<PreparedDesktopRunOverlayFile> overlayFiles;
    // These are routing paths only. Desktop run does not retain directory
    // handles or identities for the author's resource roots.
    QStringList formalRoots;
};

enum class SavedSceneLaunchPreparationError
{
    ProjectNotOpen,
    RuntimeConfigurationNeedsSave,
    SceneNotFound,
    ResourceContextUnavailable,
    UnsafeReferencePath,
    DocumentTypeMismatch,
    DirtyDocumentSnapshotUnavailable,
    DirtyDocumentSnapshotInvalid,
    OverlayPathCollision,
    OverlayLimitExceeded
};

struct SavedSceneLaunchPreparationIssue
{
    SavedSceneLaunchPreparationError error =
        SavedSceneLaunchPreparationError::ProjectNotOpen;
    QString fieldName;
    QString virtualPath;
    QString absolutePath;
    RuntimeResource::ExactSelectionError resourceSelectionError =
        RuntimeResource::ExactSelectionError::None;
    QString diagnosticCode;
};

struct SavedSceneLaunchPreparationResult
{
    std::optional<PreparedSavedSceneLaunch> prepared;
    QList<SavedSceneLaunchPreparationIssue> issues;
    QList<ProjectDocumentState> dirtyDocuments;

    bool succeeded() const
    {
        return prepared.has_value() &&
            issues.isEmpty() &&
            dirtyDocuments.isEmpty();
    }
};

// Materializes one already-saved scene without creating a session directory,
// writing project/resources/settings, or starting a process. The game process
// still performs the authoritative catalog routing and isolation preparation.
SavedSceneLaunchPreparationResult prepareSavedSceneLaunch(
    const ProjectManager& projectManager,
    const ProjectDocumentRegistry& documentRegistry,
    QStringView sceneId,
    const QList<DesktopRunDocumentSnapshot>& documentSnapshots = {});

// Prepares an in-memory target that deliberately is not persisted into the
// shared project configuration. The target uses the project's saved resource
// selection as it exists while this preparation call runs.
SavedSceneLaunchPreparationResult prepareTransientSceneLaunch(
    const ProjectManager& projectManager,
    const ProjectDocumentRegistry& documentRegistry,
    const ProjectScene& transientScene,
    EditorRun::TargetKind targetKind,
    const QList<DesktopRunDocumentSnapshot>& documentSnapshots = {});
