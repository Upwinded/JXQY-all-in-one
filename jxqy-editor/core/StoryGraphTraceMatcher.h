#pragma once

#include "StoryGraphProjectResolver.h"
#include "StoryGraphRuntimeTrace.h"

#include <QSet>
#include <QString>
#include <QVector>

#include <memory>

enum class StoryGraphTraceMatchStatus
{
    NotApplicable,
    Matched,
    Unmatched,
    Ambiguous,
    Stale
};

struct StoryGraphTraceGraphEventMatch
{
    StoryGraphTraceMatchStatus status =
        StoryGraphTraceMatchStatus::NotApplicable;
    QString nodeId;
};

struct StoryGraphTraceEventMatch
{
    quint64 sequence = 0;
    StoryGraphRuntimeTraceEventType eventType =
        StoryGraphRuntimeTraceEventType::SessionStart;
    StoryGraphTraceGraphEventMatch controlFlow;
    StoryGraphTraceGraphEventMatch storySemantics;
};

struct StoryGraphTraceGraphOverlay
{
    struct Delta
    {
        quint64 revision = 0;
        QVector<QString> addedNodeIds;
        QVector<QString> addedEdgeIds;
    };

    quint64 epoch = 0;
    quint64 revision = 0;
    QSet<QString> executedNodeIds;
    QSet<QString> executedEdgeIds;
    // Deltas are append-only within one epoch. Entry N has revision N + 1
    // and contains only IDs first published by that revision.
    QVector<Delta> deltas;
};

struct StoryGraphTraceIssuePresentation
{
    StoryGraphTraceMatchStatus status =
        StoryGraphTraceMatchStatus::NotApplicable;
    StoryGraphRuntimeTraceEvent event;
};

struct StoryGraphTraceGraphIssueSummary
{
    static constexpr qsizetype
        MaximumPresentedIssueCount = 500;

    qsizetype issueCount = 0;
    // The total remains exact while UI materialization is bounded.
    QVector<StoryGraphTraceIssuePresentation>
        presentedIssues;
};

struct StoryGraphTraceMatchResult
{
    QString sessionId;
    quint64 analysisGeneration = 0;
    quint64 revision = 0;
    QVector<StoryGraphTraceEventMatch> eventMatches;
    StoryGraphTraceGraphOverlay controlFlow;
    StoryGraphTraceGraphOverlay storySemantics;
    StoryGraphTraceGraphIssueSummary
        controlFlowIssues;
    StoryGraphTraceGraphIssueSummary
        storySemanticsIssues;
    quint64 droppedSourceLineCount = 0;

    qsizetype matchedEventCount() const;
    qsizetype ambiguousEventCount() const;
    qsizetype unmatchedEventCount() const;
    qsizetype staleEventCount() const;
    quint64 aggregationWorkItemCount() const;

    qsizetype cachedMatchedEventCount = 0;
    qsizetype cachedAmbiguousEventCount = 0;
    qsizetype cachedUnmatchedEventCount = 0;
    qsizetype cachedStaleEventCount = 0;
    quint64 cachedAggregationWorkItemCount = 0;
};

// Builds immutable source/node/edge indexes for one project-analysis
// generation, then incrementally projects trace events onto that graph.
// A node is accepted only when exactly one compatible static candidate
// remains. An executed edge is accepted only when exactly one already-existing
// static edge proves the transition; the matcher never synthesizes graph IDs.
class StoryGraphTraceMatcher
{
public:
    StoryGraphTraceMatcher();
    ~StoryGraphTraceMatcher();

    // Replaces the immutable static indexes and clears published overlays,
    // while preserving the current trace sequence and active execution source
    // context. Existing events are not replayed automatically. This lets a
    // post-discard analysis generation continue safely at N+1.
    void setProjectResult(
        const StoryGraphProjectResult& projectResult);
    // Removes static indexes and published overlays but retains trace and
    // execution context for a later setProjectResult().
    void clearProjectResult();
    void resetTrace(
        const QString& sessionId,
        quint64 lastConsumedSequence = 0);

    // Drops published matches without resetting sequence or active execution
    // provenance. The next event must continue at lastSequence() + 1 and
    // cannot create a transition edge back into the discarded overlay. This
    // also replaces the explicit replay checkpoint used when a later static
    // analysis must rematch post-clear events.
    quint64 discardMatchesRetainingExecutionState();
    // Restores the execution state captured by the latest discard without
    // replacing the current static project indexes. Published matches are
    // cleared and the next accepted event is checkpointSequence() + 1.
    bool restoreDiscardExecutionCheckpoint();
    bool hasDiscardExecutionCheckpoint() const;
    quint64 discardExecutionCheckpointSequence() const;

    bool appendEvent(
        const StoryGraphRuntimeTraceEvent& event);
    bool appendEvents(
        const QVector<StoryGraphRuntimeTraceEvent>& events,
        qsizetype firstIndex = 0);

    bool hasProjectResult() const;
    quint64 analysisGeneration() const;
    quint64 lastSequence() const;
    const StoryGraphTraceMatchResult& result() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

QString storyGraphTraceMatchStatusToString(
    StoryGraphTraceMatchStatus status);
