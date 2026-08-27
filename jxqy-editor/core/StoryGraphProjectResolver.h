#pragma once

#include "StoryGraphAnalyzer.h"
#include "StoryGraphModel.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>

enum class StoryGraphContentRootKind
{
    Active,
    DependencyId,
    Common
};

struct StoryGraphContentRoot
{
    StoryGraphContentRootKind kind =
        StoryGraphContentRootKind::Active;
    QString portableRootKey;
    int ordinal = 0;
};

enum class StoryGraphReadStatus
{
    Found,
    Missing,
    Rejected,
    Error,
    Cancelled
};

struct StoryGraphReadResult
{
    StoryGraphReadStatus status =
        StoryGraphReadStatus::Missing;
    QByteArray utf8Bytes;
    QString canonicalAbsolutePath;
    QString message;
};

enum class StoryGraphMapContextState
{
    Known,
    Assumed,
    Unknown
};

struct StoryGraphMapContext
{
    StoryGraphMapContextState state =
        StoryGraphMapContextState::Unknown;
    QString effectiveMapFolder;
};

enum class StoryGraphMapFolderResolutionStatus
{
    Resolved,
    Missing,
    Rejected,
    Error,
    ContextDependent
};

struct StoryGraphMapFolderResolution
{
    StoryGraphMapFolderResolutionStatus status =
        StoryGraphMapFolderResolutionStatus::Missing;
    QString effectiveMapFolder;
    QString message;
};

struct StoryGraphProjectBudget
{
    qsizetype maximumSingleFileBytes =
        4 * 1024 * 1024;
    qsizetype maximumTotalBytes =
        64 * 1024 * 1024;
    int maximumFileCount = 1024;
    int maximumCallDepth = 64;
};

enum class StoryGraphTargetResolutionStatus
{
    Resolved,
    Missing,
    Rejected,
    ReadError,
    Cancelled,
    ContextDependent,
    BudgetExceeded
};

struct StoryGraphTargetResolution
{
    QString callerNodeId;
    StoryGraphSourceIdentity callerSource;
    StoryGraphSourceRange callerRange;
    QString literalTarget;
    QString candidateVirtualPath;
    StoryGraphMapContext mapContext;
    StoryGraphTargetResolutionStatus status =
        StoryGraphTargetResolutionStatus::Missing;
    QString targetPortableRootKey;
    QString targetVirtualPath;
    StoryGraphEdgeKind edgeKind =
        StoryGraphEdgeKind::Call;
    QString message;
};

enum class StoryGraphProjectStatus
{
    Complete,
    Partial,
    Failed,
    Cancelled
};

struct StoryGraphProjectResult
{
    quint64 analysisGeneration = 0;
    StoryGraphProjectStatus status =
        StoryGraphProjectStatus::Complete;
    QList<StoryGraphDocumentResult> documents;
    StoryGraphResult controlFlowGraph;
    StoryGraphResult semanticGraph{
        StoryGraphKind::StorySemantics};
    QList<StoryGraphTargetResolution> targetResolutions;
    QList<StoryGraphWarning> warnings;
    qsizetype totalBytesRead = 0;

    bool hasUsableGraph() const;
    bool wasCancelled() const;
};

struct StoryGraphProjectRequest
{
    StoryGraphSourceSnapshot entrySource;
    QList<StoryGraphContentRoot> orderedContentRoots;
    QList<StoryGraphSourceSnapshot> activeRootOpenSnapshots;
    StoryGraphMapContext entryMapContext;
    StoryGraphProjectBudget budget;
    quint64 analysisGeneration = 0;
    bool includeUnknownCalls = true;
};

class StoryGraphProjectResolver
{
public:
    using ReadCallback = std::function<
        StoryGraphReadResult(
            const StoryGraphContentRoot& root,
            const QString& strictVirtualPath)>;
    using MapFolderResolverCallback = std::function<
        StoryGraphMapFolderResolution(
            const QString& strictMapTarget)>;
    using CancelCallback = std::function<bool()>;

    static StoryGraphProjectResult analyze(
        const StoryGraphProjectRequest& request,
        const ReadCallback& readCallback,
        const MapFolderResolverCallback&
            mapFolderResolver =
                MapFolderResolverCallback(),
        const CancelCallback& cancelCallback =
            CancelCallback());

    // Strict virtual paths use forward slashes and preserve the caller's
    // spelling. This validator never cleans, collapses, or makes a path
    // relative on the caller's behalf.
    static bool isStrictRelativeVirtualPath(
        const QString& value,
        QString* rejectionReason = nullptr);
};

QString storyGraphContentRootKindToString(
    StoryGraphContentRootKind kind);
// Builds the portable logical root identity shared by resource selection,
// runtime-trace matching, and stable graph source identities. resourcePackId
// is the canonical active/dependency ID when that root kind has one; it is
// empty for logical roots such as Common or dependency-by-path that do not.
QString makeStoryGraphPortableRootKey(
    StoryGraphContentRootKind kind,
    quint64 ordinal,
    const QString& resourcePackId);
QString storyGraphMapContextStateToString(
    StoryGraphMapContextState state);
QString storyGraphTargetResolutionStatusToString(
    StoryGraphTargetResolutionStatus status);
QString storyGraphProjectStatusToString(
    StoryGraphProjectStatus status);
