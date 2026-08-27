#pragma once

#include "../../src/Launch/EditorRunDescriptor.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>

struct PreparedSavedSceneLaunch;

struct DesktopRunSessionWorkspaceControl
{
    std::function<bool()> cancellationRequested;
};

struct DesktopRunSessionPaths
{
    QString sessionRoot;
    QString overlayRoot;
    QString isolatedSaveRoot;
    QString applicationStateRoot;
    QString diagnosticsRoot;
    QString diagnosticsPath;
    QString logPath;
    QString runtimeTracePath;
    QString markerPath;
    QString resourceRoutingContractPath;
    QString descriptorPath;
};

struct DesktopRunSessionWorkspaceLimits
{
    static constexpr qsizetype DefaultMaximumFormalRootCount = 256;
    static constexpr quint64 DefaultMaximumEntryCount = 2'000'000;
    static constexpr qsizetype DefaultMaximumManifestBytes =
        256 * 1024 * 1024;
    static constexpr qsizetype DefaultMaximumPathBytes =
        64 * 1024;
    static constexpr qsizetype DefaultMaximumDirectoryDepth = 256;
    static constexpr quint64 DefaultMaximumDirectoryEntryCount =
        16'384;
    static constexpr quint64 DefaultMaximumWorkingBytes =
        512ULL * 1024ULL * 1024ULL;

    qsizetype maximumFormalRootCount =
        DefaultMaximumFormalRootCount;
    quint64 maximumEntryCount = DefaultMaximumEntryCount;
    qsizetype maximumManifestBytes =
        DefaultMaximumManifestBytes;
    qsizetype maximumPathBytes =
        DefaultMaximumPathBytes;
    qsizetype maximumDirectoryDepth =
        DefaultMaximumDirectoryDepth;
    quint64 maximumDirectoryEntryCount =
        DefaultMaximumDirectoryEntryCount;

    // Bounds retained workspace preparation data plus explicitly reserved
    // buffers. It is not a process-RSS ceiling: Qt allocator bookkeeping and
    // native handle memory are not charged byte-for-byte.
    quint64 maximumWorkingBytes =
        DefaultMaximumWorkingBytes;
};

struct DesktopRunSessionFormalRoots
{
    // Routing paths only. They are not opened or retained by workspace
    // creation and may be changed, replaced, or removed by external tools.
    // The game observes their current state through ordinary file access.
    QStringList resourceRoots;

    // The active content-root path carries the formal save routing role. It
    // may use the same path as one resourceRoots entry.
    QString saveRoot;
};

struct DesktopRunTraceOverlayOrigin
{
    QString virtualPath;
    quint64 rootOrdinal = 0;
    // Stable logical trace metadata. Legacy v1 contracts may omit these
    // fields. They describe provenance only and never bind a formal root's
    // identity or contents.
    QString rootKind;
    QString resourcePackId;
    QString rootPath;
};

struct DesktopRunSessionWorkspace
{
    static constexpr int SchemaVersion = 1;

    QString sessionId;
    DesktopRunSessionPaths paths;
    EditorRun::Descriptor descriptor;
    QByteArray descriptorSha256;
    QByteArray resourceRoutingContractSha256;
    QVector<DesktopRunTraceOverlayOrigin>
        traceOverlayOrigins;
    DesktopRunSessionFormalRoots formalRoots;
    DesktopRunSessionWorkspaceLimits workspaceLimits;
};

enum class DesktopRunSessionWorkspaceError
{
    None,
    InvalidLimits,
    InvalidSessionsBase,
    SessionsBaseIsLink,
    NoRoutingRoots,
    TooManyRoutingRoots,
    InvalidRoutingRootPath,
    RoutingRootsDoNotCoverDescriptorAssets,
    PreparedLaunchDescriptorMismatch,
    PreparedOverlayInvalid,
    PreparedOverlayPathCollision,
    PreparedOverlayLimitExceeded,
    Cancelled,
    UntrustedSessionsBasePermissions,
    InvalidDescriptor,
    SessionRootCreationFailed,
    SessionLeafCreationFailed,
    SessionRoutingChanged,
    MarkerWriteFailed,
    ResourceRoutingContractWriteFailed,
    OverlayMaterializationFailed,
    OverlayPublishFailed,
    DescriptorWriteFailed
};

struct DesktopRunSessionWorkspaceResult
{
    std::optional<DesktopRunSessionWorkspace> workspace;
    // Once the exact session marker has been committed, this marker-backed
    // typed record remains available to the current coordinator for cleanup
    // even if later workspace materialization fails. It is not launchable.
    std::optional<DesktopRunSessionWorkspace> cleanupWorkspace;
    QString trustedSessionsBaseDirectory;
    QString sessionId;
    DesktopRunSessionPaths paths;
    DesktopRunSessionWorkspaceError error =
        DesktopRunSessionWorkspaceError::None;
    EditorRun::DescriptorError descriptorError =
        EditorRun::DescriptorError::None;
    QString descriptorFieldPath;
    QString problemPath;
    QString message;

    // Workspace creation itself does not delete a partially materialized root;
    // the current coordinator uses cleanupWorkspace to remove it. This path is
    // deliberately empty after SessionRoutingChanged because the lexical path
    // may no longer name the held directory identity.
    QString partialFailureRoot;

    bool succeeded() const
    {
        return workspace.has_value() &&
            error == DesktopRunSessionWorkspaceError::None;
    }
};

enum class DesktopRunSessionWorkspaceFaultPoint
{
    AfterSessionRootOpened,
    AfterSessionLeavesOpened,
    BeforeOverlayStaging,
    DuringOverlayFileWrite,
    BeforeOverlayPublish,
    AfterOverlayPublish,
    BeforeAtomicTemporaryOpen,
    AfterAtomicTemporaryOpen,
    BeforeAtomicWrite,
    BeforeAtomicFlush,
    BeforeAtomicRename
};

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
using DesktopRunSessionWorkspaceFaultInjector = std::function<bool(
    DesktopRunSessionWorkspaceFaultPoint,
    const QString&)>;
#endif

// Creates one private editor-run session beneath an already existing trusted
// sessions directory. The descriptor's business fields are retained while all
// session-specific fields are assigned by this service.
DesktopRunSessionWorkspaceResult createDesktopRunSessionWorkspace(
    const QString& trustedSessionsBaseDirectory,
    const EditorRun::Descriptor& descriptorTemplate,
    const DesktopRunSessionFormalRoots& formalRoots,
    const DesktopRunSessionWorkspaceLimits& limits =
        DesktopRunSessionWorkspaceLimits(),
    const DesktopRunSessionWorkspaceControl& control =
        DesktopRunSessionWorkspaceControl());

// Production saved-scene launches derive the descriptor, routing paths, and
// active save role from the prepared selection. No author's resource root is
// retained or identity-checked across the handoff. A non-empty prepared sparse
// overlay is revalidated, staged only beneath the private session, and
// atomically published before the descriptor becomes available.
DesktopRunSessionWorkspaceResult createDesktopRunSessionWorkspace(
    const QString& trustedSessionsBaseDirectory,
    const PreparedSavedSceneLaunch& preparedLaunch,
    const DesktopRunSessionWorkspaceLimits& limits =
        DesktopRunSessionWorkspaceLimits(),
    const DesktopRunSessionWorkspaceControl& control =
        DesktopRunSessionWorkspaceControl());

// Lower-level adapter retained for tests that intentionally exercise a
// descriptor/preparation mismatch. The assets root may use an equivalent
// normalized host-path spelling; every other business field must exactly match
// the descriptor derived from preparedLaunch. Session ID and private workspace
// paths are intentionally not compared because this service always replaces
// them with newly created session-owned values.
DesktopRunSessionWorkspaceResult createDesktopRunSessionWorkspace(
    const QString& trustedSessionsBaseDirectory,
    const EditorRun::Descriptor& descriptorTemplate,
    const PreparedSavedSceneLaunch& preparedLaunch,
    const DesktopRunSessionWorkspaceLimits& limits =
        DesktopRunSessionWorkspaceLimits(),
    const DesktopRunSessionWorkspaceControl& control =
        DesktopRunSessionWorkspaceControl());

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
void setDesktopRunSessionWorkspaceFaultInjectorForTests(
    DesktopRunSessionWorkspaceFaultInjector injector);
#endif
