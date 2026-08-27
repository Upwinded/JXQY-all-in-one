#include "StoryGraphTraceMatcher.h"

#include <QHash>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace
{
enum class SourceMatchState
{
    Valid,
    Unmatched,
    Stale
};

QString sourceKey(
    const QString& portableRootKey,
    const QString& virtualPath)
{
    return portableRootKey +
        QChar::Null +
        virtualPath;
}

QString sourceKey(
    const StoryGraphSourceIdentity& source)
{
    return sourceKey(
        source.portableRootKey,
        source.virtualPath);
}

QString edgeKey(
    const QString& fromNodeId,
    const QString& toNodeId)
{
    return fromNodeId +
        QChar::Null +
        toNodeId;
}

QString asciiCaseFold(QString value)
{
    for (int index = 0; index < value.size(); ++index)
    {
        const ushort character = value.at(index).unicode();
        if (character >= static_cast<ushort>('A') &&
            character <= static_cast<ushort>('Z'))
        {
            value[index] =
                QChar(
                    character +
                    static_cast<ushort>('a' - 'A'));
        }
    }
    return value;
}

std::optional<QString> normalizedStrictVirtualPath(
    QString path)
{
    if (path.isEmpty() ||
        path.startsWith(QLatin1Char('/')) ||
        path.startsWith(QLatin1Char('\\')) ||
        path.contains(QLatin1Char(':')))
    {
        return std::nullopt;
    }
    path.replace(
        QLatin1Char('\\'),
        QLatin1Char('/'));
    if (!StoryGraphProjectResolver::
            isStrictRelativeVirtualPath(path))
    {
        return std::nullopt;
    }
    return path;
}

bool lineIntersectsRange(
    const StoryGraphSourceRange& range,
    quint64 line)
{
    if (!range.isValid() ||
        line >
            static_cast<quint64>(
                (std::numeric_limits<int>::max)()))
    {
        return false;
    }
    const int sourceLine = static_cast<int>(line);
    if (sourceLine < range.start.line ||
        sourceLine > range.end.line)
    {
        return false;
    }
    if (sourceLine == range.end.line &&
        range.end.column <= 1)
    {
        return false;
    }
    return true;
}

bool sourceLineControlKind(
    StoryGraphNodeKind kind)
{
    switch (kind)
    {
    case StoryGraphNodeKind::Statement:
    case StoryGraphNodeKind::Condition:
    case StoryGraphNodeKind::LoopHeader:
    case StoryGraphNodeKind::Goto:
    case StoryGraphNodeKind::Return:
    case StoryGraphNodeKind::Break:
        return true;
    default:
        return false;
    }
}

bool apiCallSemanticKind(
    StoryGraphNodeKind kind)
{
    switch (kind)
    {
    case StoryGraphNodeKind::VariableRead:
    case StoryGraphNodeKind::VariableWrite:
    case StoryGraphNodeKind::Dialogue:
    case StoryGraphNodeKind::Choice:
    case StoryGraphNodeKind::SerialScriptCall:
    case StoryGraphNodeKind::ParallelScriptCall:
    case StoryGraphNodeKind::MapLoad:
    case StoryGraphNodeKind::Battle:
    case StoryGraphNodeKind::RegisteredApiCall:
    case StoryGraphNodeKind::DynamicCall:
    case StoryGraphNodeKind::UnknownCall:
        return true;
    default:
        return false;
    }
}

bool graphEventIsRelevant(
    StoryGraphKind graphKind,
    StoryGraphRuntimeTraceEventType eventType)
{
    switch (eventType)
    {
    case StoryGraphRuntimeTraceEventType::ScriptStart:
    case StoryGraphRuntimeTraceEventType::ScriptFinish:
        return true;
    case StoryGraphRuntimeTraceEventType::SourceLine:
        return graphKind == StoryGraphKind::ControlFlow;
    case StoryGraphRuntimeTraceEventType::ApiCall:
    case StoryGraphRuntimeTraceEventType::MapChange:
    case StoryGraphRuntimeTraceEventType::VariableChange:
        return graphKind == StoryGraphKind::StorySemantics;
    case StoryGraphRuntimeTraceEventType::SessionStart:
    case StoryGraphRuntimeTraceEventType::SessionFinish:
    case StoryGraphRuntimeTraceEventType::TraceDropped:
        return false;
    }
    return false;
}

bool isCompletedScriptFinish(
    const StoryGraphRuntimeTraceEvent& event)
{
    return event.type ==
            StoryGraphRuntimeTraceEventType::
                ScriptFinish &&
        event.status == QStringLiteral("completed");
}

StoryGraphTraceMatchStatus combinedEventStatus(
    const StoryGraphTraceEventMatch& event)
{
    const StoryGraphTraceMatchStatus control =
        event.controlFlow.status;
    const StoryGraphTraceMatchStatus semantic =
        event.storySemantics.status;
    const auto either =
        [control, semantic](
            StoryGraphTraceMatchStatus status)
        {
            return control == status ||
                semantic == status;
        };
    if (either(StoryGraphTraceMatchStatus::Stale))
        return StoryGraphTraceMatchStatus::Stale;
    if (either(StoryGraphTraceMatchStatus::Ambiguous))
        return StoryGraphTraceMatchStatus::Ambiguous;
    if (either(StoryGraphTraceMatchStatus::Unmatched))
        return StoryGraphTraceMatchStatus::Unmatched;
    if (either(StoryGraphTraceMatchStatus::Matched))
        return StoryGraphTraceMatchStatus::Matched;
    return StoryGraphTraceMatchStatus::NotApplicable;
}

bool graphEventIsIssue(
    StoryGraphRuntimeTraceEventType eventType,
    StoryGraphTraceMatchStatus status)
{
    return eventType ==
            StoryGraphRuntimeTraceEventType::
                TraceDropped ||
        (status !=
             StoryGraphTraceMatchStatus::Matched &&
         status !=
             StoryGraphTraceMatchStatus::
                 NotApplicable);
}

struct IndexedGraph
{
    StoryGraphResult graph;
    QHash<QString, QVector<int>> nodesBySource;
    QHash<QString, int> nodeById;
    QHash<QString, QVector<int>> edgesByEndpoints;

    void rebuild(const StoryGraphResult& sourceGraph)
    {
        graph = sourceGraph;
        nodesBySource.clear();
        nodeById.clear();
        edgesByEndpoints.clear();
        for (int index = 0;
             index < graph.nodes.size();
             ++index)
        {
            const StoryGraphNode& node =
                graph.nodes.at(index);
            nodeById.insert(node.id, index);
            if (!node.source.portableRootKey.isEmpty() &&
                !node.source.virtualPath.isEmpty())
            {
                nodesBySource[
                    sourceKey(node.source)].append(index);
            }
        }
        for (int index = 0;
             index < graph.edges.size();
             ++index)
        {
            const StoryGraphEdge& edge =
                graph.edges.at(index);
            edgesByEndpoints[
                edgeKey(
                    edge.fromNodeId,
                    edge.toNodeId)].append(index);
        }
    }

    void clear()
    {
        graph = StoryGraphResult();
        nodesBySource.clear();
        nodeById.clear();
        edgesByEndpoints.clear();
    }

    const StoryGraphNode* findNode(
        const QString& nodeId) const
    {
        const auto iterator = nodeById.constFind(nodeId);
        if (iterator == nodeById.cend())
            return nullptr;
        return &graph.nodes.at(*iterator);
    }

    QVector<const StoryGraphEdge*> exactEdges(
        const QString& fromNodeId,
        const QString& toNodeId,
        std::optional<StoryGraphEdgeKind> requiredKind =
            std::nullopt) const
    {
        QVector<const StoryGraphEdge*> result;
        const auto iterator =
            edgesByEndpoints.constFind(
                edgeKey(fromNodeId, toNodeId));
        if (iterator == edgesByEndpoints.cend())
            return result;
        for (const int index : *iterator)
        {
            const StoryGraphEdge& edge =
                graph.edges.at(index);
            if (!requiredKind ||
                edge.kind == *requiredKind)
            {
                result.append(&edge);
            }
        }
        return result;
    }
};
}

struct StoryGraphTraceMatcher::Impl
{
    struct ExecutionState
    {
        bool active = true;
        std::optional<quint64> parentExecutionId;
        StoryGraphContentRootKind rootKind =
            StoryGraphContentRootKind::Active;
        quint64 rootOrdinal = 0;
        QString resourcePackId;
        QString portableRootKey;
        QString virtualPath;
        QByteArray contentSha256;
        SourceMatchState sourceMatchState =
            SourceMatchState::Unmatched;
        std::optional<quint64> currentLine;
        QString lastControlNodeId;
        QString lastSemanticNodeId;
        QVector<QString> pendingSemanticCallNodeIds;
    };

    struct ExecutionCheckpoint
    {
        QHash<quint64, ExecutionState> executions;
        quint64 consumedSequence = 0;
        bool sessionStarted = false;
        bool sessionFinished = false;
    };

    bool projectAvailable = false;
    IndexedGraph controlFlow;
    IndexedGraph storySemantics;
    QSet<QString> knownSourceKeys;
    QHash<QString, QSet<QByteArray>> sourceDigests;
    QHash<quint64, ExecutionState> executions;
    StoryGraphTraceMatchResult matchResult;
    quint64 projectGeneration = 0;
    quint64 consumedSequence = 0;
    bool sessionStarted = false;
    bool sessionFinished = false;
    std::optional<ExecutionCheckpoint> discardCheckpoint;
    quint64 resultRevisionCounter = 0;
    quint64 overlayEpochCounter = 0;

    static void advanceCounter(quint64& counter)
    {
        ++counter;
        if (counter == 0)
            ++counter;
    }

    void advanceResultRevision()
    {
        advanceCounter(resultRevisionCounter);
        matchResult.revision =
            resultRevisionCounter;
    }

    void initializeOverlayEpochs()
    {
        advanceCounter(overlayEpochCounter);
        matchResult.controlFlow.epoch =
            overlayEpochCounter;
        matchResult.storySemantics.epoch =
            overlayEpochCounter;
    }

    static void appendIssuePresentation(
        StoryGraphTraceGraphIssueSummary& summary,
        const StoryGraphRuntimeTraceEvent& event,
        const StoryGraphTraceGraphEventMatch& match)
    {
        if (!graphEventIsIssue(
                event.type,
                match.status))
        {
            return;
        }

        ++summary.issueCount;
        if (summary.presentedIssues.size() >=
            StoryGraphTraceGraphIssueSummary::
                MaximumPresentedIssueCount)
        {
            return;
        }
        StoryGraphTraceIssuePresentation
            presentation;
        presentation.status = match.status;
        presentation.event = event;
        summary.presentedIssues.append(
            std::move(presentation));
    }

    static void finalizeOverlayDelta(
        StoryGraphTraceGraphOverlay& overlay,
        StoryGraphTraceGraphOverlay::Delta& delta)
    {
        if (delta.addedNodeIds.isEmpty() &&
            delta.addedEdgeIds.isEmpty())
        {
            return;
        }
        advanceCounter(overlay.revision);
        delta.revision = overlay.revision;
        overlay.deltas.append(std::move(delta));
    }

    void registerSource(
        const StoryGraphSourceIdentity& source)
    {
        if (source.portableRootKey.isEmpty() ||
            source.virtualPath.isEmpty())
        {
            return;
        }
        const QString key = sourceKey(source);
        knownSourceKeys.insert(key);
        if (!source.contentSha256.isEmpty())
            sourceDigests[key].insert(source.contentSha256);
    }

    SourceMatchState resolveSource(
        const ExecutionState& execution) const
    {
        const QString key =
            sourceKey(
                execution.portableRootKey,
                execution.virtualPath);
        if (!knownSourceKeys.contains(key))
            return SourceMatchState::Unmatched;
        const auto digest = sourceDigests.constFind(key);
        if (digest != sourceDigests.cend() &&
            digest->contains(execution.contentSha256))
        {
            return SourceMatchState::Valid;
        }
        return SourceMatchState::Stale;
    }

    void clearPublishedMatches()
    {
        matchResult.eventMatches.clear();
        matchResult.controlFlow =
            StoryGraphTraceGraphOverlay();
        matchResult.storySemantics =
            StoryGraphTraceGraphOverlay();
        matchResult.controlFlowIssues =
            StoryGraphTraceGraphIssueSummary();
        matchResult.storySemanticsIssues =
            StoryGraphTraceGraphIssueSummary();
        matchResult.droppedSourceLineCount = 0;
        matchResult.cachedMatchedEventCount = 0;
        matchResult.cachedAmbiguousEventCount = 0;
        matchResult.cachedUnmatchedEventCount = 0;
        matchResult.cachedStaleEventCount = 0;
        matchResult.cachedAggregationWorkItemCount = 0;
        initializeOverlayEpochs();
        advanceResultRevision();
    }

    void breakTransitions()
    {
        for (auto execution = executions.begin();
             execution != executions.end();
             ++execution)
        {
            execution->lastControlNodeId.clear();
            execution->lastSemanticNodeId.clear();
            execution->
                pendingSemanticCallNodeIds.clear();
        }
    }

    void rebuildProject(
        const StoryGraphProjectResult& projectResult)
    {
        projectAvailable = true;
        controlFlow.rebuild(
            projectResult.controlFlowGraph);
        storySemantics.rebuild(
            projectResult.semanticGraph);
        knownSourceKeys.clear();
        sourceDigests.clear();
        for (const StoryGraphDocumentResult& document :
             projectResult.documents)
        {
            registerSource(document.source);
        }
        for (const StoryGraphNode& node :
             projectResult.controlFlowGraph.nodes)
        {
            registerSource(node.source);
        }
        for (const StoryGraphNode& node :
             projectResult.semanticGraph.nodes)
        {
            registerSource(node.source);
        }
        for (auto execution = executions.begin();
             execution != executions.end();
             ++execution)
        {
            execution->sourceMatchState =
                resolveSource(*execution);
        }
        clearPublishedMatches();
        breakTransitions();
        projectGeneration =
            projectResult.analysisGeneration;
        matchResult.analysisGeneration =
            projectGeneration;
    }

    StoryGraphTraceGraphEventMatch matchGraphEvent(
        const IndexedGraph& index,
        StoryGraphKind graphKind,
        const StoryGraphRuntimeTraceEvent& event,
        const ExecutionState* execution) const
    {
        StoryGraphTraceGraphEventMatch result;
        if (!graphEventIsRelevant(graphKind, event.type))
            return result;
        if (event.type ==
                StoryGraphRuntimeTraceEventType::
                    ScriptFinish &&
            !isCompletedScriptFinish(event))
        {
            return result;
        }
        if (execution == nullptr &&
            (event.type ==
                 StoryGraphRuntimeTraceEventType::
                     MapChange ||
             event.type ==
                 StoryGraphRuntimeTraceEventType::
                     VariableChange))
        {
            return result;
        }
        if (execution == nullptr)
        {
            result.status =
                StoryGraphTraceMatchStatus::Unmatched;
            return result;
        }
        if (execution->sourceMatchState ==
            SourceMatchState::Stale)
        {
            result.status =
                StoryGraphTraceMatchStatus::Stale;
            return result;
        }
        if (!projectAvailable ||
            execution->sourceMatchState ==
                SourceMatchState::Unmatched)
        {
            result.status =
                StoryGraphTraceMatchStatus::Unmatched;
            return result;
        }

        const QString key =
            sourceKey(
                execution->portableRootKey,
                execution->virtualPath);
        const auto nodeIndexes =
            index.nodesBySource.constFind(key);
        QVector<const StoryGraphNode*> candidates;
        if (nodeIndexes !=
            index.nodesBySource.cend())
        {
            for (const int nodeIndex : *nodeIndexes)
            {
                const StoryGraphNode& node =
                    index.graph.nodes.at(nodeIndex);
                if (node.source.contentSha256 !=
                    execution->contentSha256)
                {
                    continue;
                }
                bool compatible = false;
                switch (event.type)
                {
                case StoryGraphRuntimeTraceEventType::
                    ScriptStart:
                    compatible =
                        node.kind ==
                            StoryGraphNodeKind::
                                ChunkEntry;
                    break;
                case StoryGraphRuntimeTraceEventType::
                    ScriptFinish:
                    compatible =
                        node.kind ==
                            StoryGraphNodeKind::
                                ChunkExit;
                    break;
                case StoryGraphRuntimeTraceEventType::
                    SourceLine:
                    compatible =
                        graphKind ==
                            StoryGraphKind::
                                ControlFlow &&
                        sourceLineControlKind(
                            node.kind) &&
                        lineIntersectsRange(
                            node.sourceRange,
                            event.line);
                    break;
                case StoryGraphRuntimeTraceEventType::
                    ApiCall:
                    compatible =
                        graphKind ==
                            StoryGraphKind::
                                StorySemantics &&
                        execution->currentLine &&
                        apiCallSemanticKind(
                            node.kind) &&
                        node.apiName ==
                            event.apiName &&
                        lineIntersectsRange(
                            node.sourceRange,
                            *execution->
                                currentLine);
                    break;
                case StoryGraphRuntimeTraceEventType::
                    MapChange:
                {
                    const std::optional<QString>
                        nodeTarget =
                            normalizedStrictVirtualPath(
                                node.literalTarget);
                    const std::optional<QString>
                        eventTarget =
                            normalizedStrictVirtualPath(
                                event.target);
                    compatible =
                        graphKind ==
                            StoryGraphKind::
                                StorySemantics &&
                        execution->currentLine &&
                        node.kind ==
                            StoryGraphNodeKind::
                                MapLoad &&
                        nodeTarget &&
                        eventTarget &&
                        *nodeTarget == *eventTarget &&
                        lineIntersectsRange(
                            node.sourceRange,
                            *execution->
                                currentLine);
                    break;
                }
                case StoryGraphRuntimeTraceEventType::
                    VariableChange:
                    compatible =
                        graphKind ==
                            StoryGraphKind::
                                StorySemantics &&
                        execution->currentLine &&
                        node.kind ==
                            StoryGraphNodeKind::
                                VariableWrite &&
                        asciiCaseFold(
                            node.variableName) ==
                            event.variableName &&
                        lineIntersectsRange(
                            node.sourceRange,
                            *execution->
                                currentLine);
                    break;
                case StoryGraphRuntimeTraceEventType::
                    SessionStart:
                case StoryGraphRuntimeTraceEventType::
                    SessionFinish:
                case StoryGraphRuntimeTraceEventType::
                    TraceDropped:
                    break;
                }
                if (compatible)
                    candidates.append(&node);
            }
        }

        if (candidates.isEmpty())
        {
            result.status =
                StoryGraphTraceMatchStatus::Unmatched;
        }
        else if (candidates.size() == 1)
        {
            result.status =
                StoryGraphTraceMatchStatus::Matched;
            result.nodeId = candidates.constFirst()->id;
        }
        else
        {
            result.status =
                StoryGraphTraceMatchStatus::Ambiguous;
        }
        return result;
    }

    void applyGraphMatch(
        const IndexedGraph& index,
        const StoryGraphTraceGraphEventMatch& match,
        QString& lastNodeId,
        StoryGraphTraceGraphOverlay& overlay,
        StoryGraphTraceGraphOverlay::Delta& delta)
    {
        if (match.status ==
            StoryGraphTraceMatchStatus::Matched)
        {
            if (!match.nodeId.isEmpty() &&
                !overlay.executedNodeIds.contains(
                    match.nodeId))
            {
                overlay.executedNodeIds.insert(
                    match.nodeId);
                delta.addedNodeIds.append(
                    match.nodeId);
            }
            if (!lastNodeId.isEmpty())
            {
                const QVector<const StoryGraphEdge*> edges =
                    index.exactEdges(
                        lastNodeId,
                        match.nodeId);
                if (edges.size() == 1 &&
                    !edges.constFirst()->id.isEmpty() &&
                    !overlay.executedEdgeIds.contains(
                        edges.constFirst()->id))
                {
                    overlay.executedEdgeIds.insert(
                        edges.constFirst()->id);
                    delta.addedEdgeIds.append(
                        edges.constFirst()->id);
                }
            }
            lastNodeId = match.nodeId;
            return;
        }
        if (match.status !=
            StoryGraphTraceMatchStatus::NotApplicable)
        {
            lastNodeId.clear();
        }
    }

    void linkChildScriptCall(
        const StoryGraphRuntimeTraceEvent& event,
        const StoryGraphTraceGraphEventMatch&
            semanticMatch,
        StoryGraphTraceGraphOverlay::Delta&
            semanticDelta)
    {
        if (!event.parentExecutionId ||
            semanticMatch.status !=
                StoryGraphTraceMatchStatus::Matched)
        {
            return;
        }
        auto parent =
            executions.find(*event.parentExecutionId);
        if (parent == executions.end())
            return;

        QHash<QString, QString> callToEdge;
        for (const QString& callNodeId :
             parent->pendingSemanticCallNodeIds)
        {
            if (callToEdge.contains(callNodeId))
                continue;
            const StoryGraphNode* callNode =
                storySemantics.findNode(callNodeId);
            if (callNode == nullptr)
                continue;
            std::optional<StoryGraphEdgeKind>
                requiredKind;
            if (callNode->kind ==
                StoryGraphNodeKind::SerialScriptCall)
            {
                requiredKind =
                    StoryGraphEdgeKind::Call;
            }
            else if (callNode->kind ==
                     StoryGraphNodeKind::
                         ParallelScriptCall)
            {
                requiredKind =
                    StoryGraphEdgeKind::ParallelCall;
            }
            else
            {
                continue;
            }
            const QVector<const StoryGraphEdge*> edges =
                storySemantics.exactEdges(
                    callNodeId,
                    semanticMatch.nodeId,
                    requiredKind);
            if (edges.size() == 1)
            {
                callToEdge.insert(
                    callNodeId,
                    edges.constFirst()->id);
            }
        }
        if (callToEdge.size() != 1)
            return;

        const auto candidate =
            callToEdge.constBegin();
        if (!candidate.value().isEmpty() &&
            !matchResult.storySemantics.
                 executedEdgeIds.contains(
                     candidate.value()))
        {
            matchResult.storySemantics.
                executedEdgeIds.insert(
                    candidate.value());
            semanticDelta.addedEdgeIds.append(
                candidate.value());
        }
        const int pendingIndex =
            parent->pendingSemanticCallNodeIds.
                indexOf(candidate.key());
        if (pendingIndex >= 0)
        {
            parent->pendingSemanticCallNodeIds.
                removeAt(pendingIndex);
        }
    }
};

qsizetype StoryGraphTraceMatchResult::
matchedEventCount() const
{
    return cachedMatchedEventCount;
}

qsizetype StoryGraphTraceMatchResult::
ambiguousEventCount() const
{
    return cachedAmbiguousEventCount;
}

qsizetype StoryGraphTraceMatchResult::
unmatchedEventCount() const
{
    return cachedUnmatchedEventCount;
}

qsizetype StoryGraphTraceMatchResult::
staleEventCount() const
{
    return cachedStaleEventCount;
}

quint64 StoryGraphTraceMatchResult::
aggregationWorkItemCount() const
{
    return cachedAggregationWorkItemCount;
}

StoryGraphTraceMatcher::StoryGraphTraceMatcher()
    : impl(std::make_unique<Impl>())
{
}

StoryGraphTraceMatcher::~StoryGraphTraceMatcher() =
    default;

void StoryGraphTraceMatcher::setProjectResult(
    const StoryGraphProjectResult& projectResult)
{
    impl->rebuildProject(projectResult);
}

void StoryGraphTraceMatcher::clearProjectResult()
{
    impl->projectAvailable = false;
    impl->controlFlow.clear();
    impl->storySemantics.clear();
    impl->knownSourceKeys.clear();
    impl->sourceDigests.clear();
    impl->clearPublishedMatches();
    impl->breakTransitions();
    impl->matchResult.analysisGeneration = 0;
    impl->projectGeneration = 0;
    for (auto execution = impl->executions.begin();
         execution != impl->executions.end();
         ++execution)
    {
        execution->sourceMatchState =
            SourceMatchState::Unmatched;
    }
}

void StoryGraphTraceMatcher::resetTrace(
    const QString& sessionId,
    quint64 lastConsumedSequence)
{
    impl->executions.clear();
    impl->consumedSequence = lastConsumedSequence;
    impl->sessionStarted =
        lastConsumedSequence > 0;
    impl->sessionFinished = false;
    impl->discardCheckpoint.reset();
    impl->matchResult =
        StoryGraphTraceMatchResult();
    impl->matchResult.sessionId = sessionId;
    if (impl->projectAvailable)
        impl->matchResult.analysisGeneration =
            impl->projectGeneration;
    impl->initializeOverlayEpochs();
    impl->advanceResultRevision();
}

quint64 StoryGraphTraceMatcher::
discardMatchesRetainingExecutionState()
{
    impl->clearPublishedMatches();
    impl->breakTransitions();
    Impl::ExecutionCheckpoint checkpoint;
    checkpoint.executions = impl->executions;
    checkpoint.consumedSequence =
        impl->consumedSequence;
    checkpoint.sessionStarted =
        impl->sessionStarted;
    checkpoint.sessionFinished =
        impl->sessionFinished;
    impl->discardCheckpoint =
        std::move(checkpoint);
    return impl->consumedSequence;
}

bool StoryGraphTraceMatcher::
restoreDiscardExecutionCheckpoint()
{
    if (!impl->discardCheckpoint)
        return false;

    impl->executions =
        impl->discardCheckpoint->executions;
    impl->consumedSequence =
        impl->discardCheckpoint->
            consumedSequence;
    impl->sessionStarted =
        impl->discardCheckpoint->sessionStarted;
    impl->sessionFinished =
        impl->discardCheckpoint->sessionFinished;
    for (auto execution = impl->executions.begin();
         execution != impl->executions.end();
         ++execution)
    {
        execution->sourceMatchState =
            impl->resolveSource(*execution);
    }
    impl->clearPublishedMatches();
    impl->matchResult.analysisGeneration =
        impl->projectAvailable
        ? impl->projectGeneration
        : 0;
    return true;
}

bool StoryGraphTraceMatcher::
hasDiscardExecutionCheckpoint() const
{
    return impl->discardCheckpoint.has_value();
}

quint64 StoryGraphTraceMatcher::
discardExecutionCheckpointSequence() const
{
    return impl->discardCheckpoint
        ? impl->discardCheckpoint->
              consumedSequence
        : 0;
}

bool StoryGraphTraceMatcher::appendEvent(
    const StoryGraphRuntimeTraceEvent& event)
{
    if (impl->sessionFinished ||
        event.sequence !=
            impl->consumedSequence + 1)
    {
        return false;
    }

    const auto activeExecution =
        [this](quint64 executionId)
        {
            const auto execution =
                impl->executions.constFind(
                    executionId);
            return execution !=
                    impl->executions.cend() &&
                execution->active;
        };

    if (event.type ==
        StoryGraphRuntimeTraceEventType::SessionStart)
    {
        if (impl->sessionStarted ||
            event.sequence != 1)
        {
            return false;
        }
    }
    else if (!impl->sessionStarted)
    {
        return false;
    }

    switch (event.type)
    {
    case StoryGraphRuntimeTraceEventType::ScriptStart:
        if (!event.executionId ||
            impl->executions.contains(
                *event.executionId) ||
            (event.parentExecutionId &&
             !impl->executions.contains(
                 *event.parentExecutionId)))
        {
            return false;
        }
        break;
    case StoryGraphRuntimeTraceEventType::ScriptFinish:
    case StoryGraphRuntimeTraceEventType::SourceLine:
    case StoryGraphRuntimeTraceEventType::ApiCall:
        if (!event.executionId ||
            !activeExecution(*event.executionId))
        {
            return false;
        }
        break;
    case StoryGraphRuntimeTraceEventType::MapChange:
    case StoryGraphRuntimeTraceEventType::VariableChange:
        if (event.executionId &&
            !activeExecution(*event.executionId))
        {
            return false;
        }
        break;
    case StoryGraphRuntimeTraceEventType::SessionFinish:
        if (std::any_of(
                impl->executions.cbegin(),
                impl->executions.cend(),
                [](const Impl::ExecutionState& execution)
                {
                    return execution.active;
                }))
        {
            return false;
        }
        break;
    case StoryGraphRuntimeTraceEventType::SessionStart:
    case StoryGraphRuntimeTraceEventType::TraceDropped:
        break;
    }

    if (event.type ==
        StoryGraphRuntimeTraceEventType::ScriptStart)
    {
        Impl::ExecutionState execution;
        execution.parentExecutionId =
            event.parentExecutionId;
        execution.rootKind = event.rootKind;
        execution.rootOrdinal = event.rootOrdinal;
        execution.resourcePackId =
            event.resourcePackId.value_or(QString());
        execution.portableRootKey =
            makeStoryGraphPortableRootKey(
                execution.rootKind,
                execution.rootOrdinal,
                execution.resourcePackId);
        execution.virtualPath = event.virtualPath;
        execution.contentSha256 =
            event.contentSha256;
        execution.sourceMatchState =
            impl->resolveSource(execution);
        impl->executions.insert(
            *event.executionId,
            std::move(execution));
    }

    Impl::ExecutionState* execution = nullptr;
    if (event.executionId)
    {
        auto iterator =
            impl->executions.find(*event.executionId);
        if (iterator != impl->executions.end())
            execution = &*iterator;
    }
    if (event.type ==
            StoryGraphRuntimeTraceEventType::SourceLine &&
        execution != nullptr)
    {
        execution->currentLine = event.line;
    }

    StoryGraphTraceEventMatch eventMatch;
    eventMatch.sequence = event.sequence;
    eventMatch.eventType = event.type;
    eventMatch.controlFlow =
        impl->matchGraphEvent(
            impl->controlFlow,
            StoryGraphKind::ControlFlow,
            event,
            execution);
    eventMatch.storySemantics =
        impl->matchGraphEvent(
            impl->storySemantics,
            StoryGraphKind::StorySemantics,
            event,
            execution);
    StoryGraphTraceGraphOverlay::Delta
        controlFlowDelta;
    StoryGraphTraceGraphOverlay::Delta
        storySemanticsDelta;

    if (execution != nullptr)
    {
        impl->applyGraphMatch(
            impl->controlFlow,
            eventMatch.controlFlow,
            execution->lastControlNodeId,
            impl->matchResult.controlFlow,
            controlFlowDelta);
        impl->applyGraphMatch(
            impl->storySemantics,
            eventMatch.storySemantics,
            execution->lastSemanticNodeId,
            impl->matchResult.storySemantics,
            storySemanticsDelta);
        if (event.type ==
                StoryGraphRuntimeTraceEventType::
                    ScriptFinish &&
            !isCompletedScriptFinish(event))
        {
            execution->currentLine.reset();
            execution->lastControlNodeId.clear();
            execution->lastSemanticNodeId.clear();
            execution->
                pendingSemanticCallNodeIds.clear();
        }
    }

    if (event.type ==
            StoryGraphRuntimeTraceEventType::ApiCall &&
        execution != nullptr &&
        eventMatch.storySemantics.status ==
            StoryGraphTraceMatchStatus::Matched)
    {
        const StoryGraphNode* node =
            impl->storySemantics.findNode(
                eventMatch.storySemantics.nodeId);
        if (node != nullptr &&
            (node->kind ==
                 StoryGraphNodeKind::SerialScriptCall ||
             node->kind ==
                 StoryGraphNodeKind::ParallelScriptCall))
        {
            execution->
                pendingSemanticCallNodeIds.append(
                    node->id);
        }
    }
    if (event.type ==
        StoryGraphRuntimeTraceEventType::ScriptStart)
    {
        impl->linkChildScriptCall(
            event,
            eventMatch.storySemantics,
            storySemanticsDelta);
    }
    if (event.type ==
        StoryGraphRuntimeTraceEventType::TraceDropped)
    {
        impl->matchResult.droppedSourceLineCount +=
            event.droppedSourceLineCount;
        for (auto iterator =
                 impl->executions.begin();
             iterator != impl->executions.end();
             ++iterator)
        {
            iterator->currentLine.reset();
            iterator->lastControlNodeId.clear();
            iterator->lastSemanticNodeId.clear();
        }
    }

    if (event.type ==
        StoryGraphRuntimeTraceEventType::ScriptFinish)
    {
        execution->active = false;
    }
    else if (event.type ==
             StoryGraphRuntimeTraceEventType::SessionStart)
    {
        impl->sessionStarted = true;
    }
    else if (event.type ==
             StoryGraphRuntimeTraceEventType::SessionFinish)
    {
        impl->sessionFinished = true;
    }

    Impl::appendIssuePresentation(
        impl->matchResult.controlFlowIssues,
        event,
        eventMatch.controlFlow);
    Impl::appendIssuePresentation(
        impl->matchResult.storySemanticsIssues,
        event,
        eventMatch.storySemantics);
    switch (combinedEventStatus(eventMatch))
    {
    case StoryGraphTraceMatchStatus::Matched:
        ++impl->matchResult.
            cachedMatchedEventCount;
        break;
    case StoryGraphTraceMatchStatus::Ambiguous:
        ++impl->matchResult.
            cachedAmbiguousEventCount;
        break;
    case StoryGraphTraceMatchStatus::Unmatched:
        ++impl->matchResult.
            cachedUnmatchedEventCount;
        break;
    case StoryGraphTraceMatchStatus::Stale:
        ++impl->matchResult.
            cachedStaleEventCount;
        break;
    case StoryGraphTraceMatchStatus::NotApplicable:
        break;
    }
    ++impl->matchResult.
        cachedAggregationWorkItemCount;
    Impl::finalizeOverlayDelta(
        impl->matchResult.controlFlow,
        controlFlowDelta);
    Impl::finalizeOverlayDelta(
        impl->matchResult.storySemantics,
        storySemanticsDelta);
    impl->matchResult.eventMatches.append(
        std::move(eventMatch));
    impl->consumedSequence = event.sequence;
    impl->advanceResultRevision();
    return true;
}

bool StoryGraphTraceMatcher::appendEvents(
    const QVector<StoryGraphRuntimeTraceEvent>& events,
    qsizetype firstIndex)
{
    if (firstIndex < 0 ||
        firstIndex > events.size())
    {
        return false;
    }
    for (qsizetype index = firstIndex;
         index < events.size();
         ++index)
    {
        if (!appendEvent(events.at(index)))
            return false;
    }
    return true;
}

bool StoryGraphTraceMatcher::hasProjectResult() const
{
    return impl->projectAvailable;
}

quint64 StoryGraphTraceMatcher::
analysisGeneration() const
{
    return impl->matchResult.analysisGeneration;
}

quint64 StoryGraphTraceMatcher::lastSequence() const
{
    return impl->consumedSequence;
}

const StoryGraphTraceMatchResult&
StoryGraphTraceMatcher::result() const
{
    return impl->matchResult;
}

QString storyGraphTraceMatchStatusToString(
    StoryGraphTraceMatchStatus status)
{
    switch (status)
    {
    case StoryGraphTraceMatchStatus::NotApplicable:
        return QStringLiteral("not-applicable");
    case StoryGraphTraceMatchStatus::Matched:
        return QStringLiteral("matched");
    case StoryGraphTraceMatchStatus::Unmatched:
        return QStringLiteral("unmatched");
    case StoryGraphTraceMatchStatus::Ambiguous:
        return QStringLiteral("ambiguous");
    case StoryGraphTraceMatchStatus::Stale:
        return QStringLiteral("stale");
    }
    return QStringLiteral("invalid");
}
