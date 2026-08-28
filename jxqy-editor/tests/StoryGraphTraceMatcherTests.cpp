#include "../core/StoryGraphTraceMatcher.h"

#include <QCoreApplication>

#include <algorithm>
#include <iostream>

namespace
{
const QString SessionId =
    QStringLiteral(
        "11111111-2222-4333-8444-555555555555");
const QString ResourcePackId =
    QStringLiteral("剑侠二");
const QByteArray ContentDigest(32, 'a');

bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

StoryGraphSourceRange sourceRange(int line)
{
    StoryGraphSourceRange range;
    range.start.line = line;
    range.start.column = 1;
    range.start.utf16Offset = line * 10;
    range.start.utf8ByteOffset = line * 10;
    range.end.line = line;
    range.end.column = 9;
    range.end.utf16Offset = line * 10 + 8;
    range.end.utf8ByteOffset = line * 10 + 8;
    return range;
}

StoryGraphSourceIdentity sourceIdentity(
    const QString& virtualPath,
    const QByteArray& digest = ContentDigest)
{
    StoryGraphSourceIdentity source;
    source.portableRootKey =
        makeStoryGraphPortableRootKey(
            StoryGraphContentRootKind::Active,
            0,
            ResourcePackId);
    source.virtualPath = virtualPath;
    source.contentSha256 = digest;
    return source;
}

StoryGraphNode node(
    const QString& id,
    StoryGraphNodeKind kind,
    const StoryGraphSourceIdentity& source,
    int line)
{
    StoryGraphNode result;
    result.id = id;
    result.kind = kind;
    result.source = source;
    result.sourceRange = sourceRange(line);
    return result;
}

StoryGraphEdge edge(
    const QString& id,
    const QString& from,
    const QString& to,
    StoryGraphEdgeKind kind =
        StoryGraphEdgeKind::Sequential)
{
    StoryGraphEdge result;
    result.id = id;
    result.fromNodeId = from;
    result.toNodeId = to;
    result.kind = kind;
    return result;
}

StoryGraphProjectResult makeProject(
    quint64 generation = 42)
{
    const StoryGraphSourceIdentity parentSource =
        sourceIdentity(
            QStringLiteral("script/parent.txt"));
    const StoryGraphSourceIdentity childSource =
        sourceIdentity(
            QStringLiteral("script/child.txt"));

    StoryGraphProjectResult project;
    project.analysisGeneration = generation;
    StoryGraphDocumentResult parentDocument;
    parentDocument.source = parentSource;
    parentDocument.analysisGeneration = generation;
    StoryGraphDocumentResult childDocument;
    childDocument.source = childSource;
    childDocument.analysisGeneration = generation;
    project.documents = {
        parentDocument,
        childDocument
    };

    project.controlFlowGraph.kind =
        StoryGraphKind::ControlFlow;
    project.controlFlowGraph.nodes = {
        node(QStringLiteral("c-parent-entry"),
             StoryGraphNodeKind::ChunkEntry,
             parentSource,
             1),
        node(QStringLiteral("c-parent-line"),
             StoryGraphNodeKind::Statement,
             parentSource,
             2),
        node(QStringLiteral("c-parent-next"),
             StoryGraphNodeKind::Statement,
             parentSource,
             3),
        node(QStringLiteral("c-parent-exit"),
             StoryGraphNodeKind::ChunkExit,
             parentSource,
             4),
        node(QStringLiteral("c-child-entry"),
             StoryGraphNodeKind::ChunkEntry,
             childSource,
             1),
        node(QStringLiteral("c-child-line"),
             StoryGraphNodeKind::Statement,
             childSource,
             1),
        node(QStringLiteral("c-child-exit"),
             StoryGraphNodeKind::ChunkExit,
             childSource,
             2)
    };
    project.controlFlowGraph.edges = {
        edge(QStringLiteral("c-parent-entry-line"),
             QStringLiteral("c-parent-entry"),
             QStringLiteral("c-parent-line")),
        edge(QStringLiteral("c-parent-line-next"),
             QStringLiteral("c-parent-line"),
             QStringLiteral("c-parent-next")),
        edge(QStringLiteral("c-parent-line-exit"),
             QStringLiteral("c-parent-line"),
             QStringLiteral("c-parent-exit")),
        edge(QStringLiteral("c-child-entry-line"),
             QStringLiteral("c-child-entry"),
             QStringLiteral("c-child-line")),
        edge(QStringLiteral("c-child-line-exit"),
             QStringLiteral("c-child-line"),
             QStringLiteral("c-child-exit"))
    };

    project.semanticGraph.kind =
        StoryGraphKind::StorySemantics;
    StoryGraphNode parentCall =
        node(
            QStringLiteral("s-parent-call"),
            StoryGraphNodeKind::SerialScriptCall,
            parentSource,
            2);
    parentCall.apiName =
        QStringLiteral("runscript");
    parentCall.literalTarget =
        QStringLiteral("child.txt");
    StoryGraphNode parentVariable =
        node(
            QStringLiteral("s-parent-variable"),
            StoryGraphNodeKind::VariableWrite,
            parentSource,
            2);
    parentVariable.variableName =
        QStringLiteral("score");
    StoryGraphNode childTalk =
        node(
            QStringLiteral("s-child-talk"),
            StoryGraphNodeKind::RegisteredApiCall,
            childSource,
            1);
    childTalk.apiName = QStringLiteral("talk");
    project.semanticGraph.nodes = {
        node(QStringLiteral("s-parent-entry"),
             StoryGraphNodeKind::ChunkEntry,
             parentSource,
             1),
        parentCall,
        parentVariable,
        node(QStringLiteral("s-parent-exit"),
             StoryGraphNodeKind::ChunkExit,
             parentSource,
             4),
        node(QStringLiteral("s-child-entry"),
             StoryGraphNodeKind::ChunkEntry,
             childSource,
             1),
        childTalk,
        node(QStringLiteral("s-child-exit"),
             StoryGraphNodeKind::ChunkExit,
             childSource,
             2)
    };
    project.semanticGraph.edges = {
        edge(QStringLiteral("s-parent-entry-call"),
             QStringLiteral("s-parent-entry"),
             QStringLiteral("s-parent-call")),
        edge(QStringLiteral("s-parent-call-exit"),
             QStringLiteral("s-parent-call"),
             QStringLiteral("s-parent-exit")),
        edge(QStringLiteral("s-call-child-entry"),
             QStringLiteral("s-parent-call"),
             QStringLiteral("s-child-entry"),
             StoryGraphEdgeKind::Call),
        edge(QStringLiteral("s-child-entry-talk"),
             QStringLiteral("s-child-entry"),
             QStringLiteral("s-child-talk")),
        edge(QStringLiteral("s-child-talk-exit"),
             QStringLiteral("s-child-talk"),
             QStringLiteral("s-child-exit"))
    };
    return project;
}

StoryGraphRuntimeTraceEvent event(
    quint64 sequence,
    StoryGraphRuntimeTraceEventType type)
{
    StoryGraphRuntimeTraceEvent result;
    result.sequence = sequence;
    result.type = type;
    return result;
}

StoryGraphRuntimeTraceEvent scriptStartEvent(
    quint64 sequence,
    quint64 executionId,
    const QString& virtualPath,
    std::optional<quint64> parentExecutionId =
        std::nullopt,
    const QByteArray& digest = ContentDigest)
{
    StoryGraphRuntimeTraceEvent result =
        event(
            sequence,
            StoryGraphRuntimeTraceEventType::
                ScriptStart);
    result.executionId = executionId;
    result.parentExecutionId = parentExecutionId;
    result.virtualPath = virtualPath;
    result.contentSha256 = digest;
    result.rootKind =
        StoryGraphContentRootKind::Active;
    result.rootOrdinal = 0;
    result.resourcePackId = ResourcePackId;
    result.sourceLayer =
        StoryGraphRuntimeTraceSourceLayer::Formal;
    return result;
}

StoryGraphRuntimeTraceEvent executionEvent(
    quint64 sequence,
    StoryGraphRuntimeTraceEventType type,
    quint64 executionId)
{
    StoryGraphRuntimeTraceEvent result =
        event(sequence, type);
    result.executionId = executionId;
    return result;
}

QVector<StoryGraphRuntimeTraceEvent>
completeTrace()
{
    QVector<StoryGraphRuntimeTraceEvent> events;
    events.append(
        event(
            1,
            StoryGraphRuntimeTraceEventType::
                SessionStart));
    events.append(
        scriptStartEvent(
            2,
            1,
            QStringLiteral("script/parent.txt")));
    StoryGraphRuntimeTraceEvent parentLine =
        executionEvent(
            3,
            StoryGraphRuntimeTraceEventType::
                SourceLine,
            1);
    parentLine.line = 2;
    events.append(parentLine);
    StoryGraphRuntimeTraceEvent parentCall =
        executionEvent(
            4,
            StoryGraphRuntimeTraceEventType::
                ApiCall,
            1);
    parentCall.apiName =
        QStringLiteral("runscript");
    events.append(parentCall);
    events.append(
        scriptStartEvent(
            5,
            2,
            QStringLiteral("script/child.txt"),
            1));
    StoryGraphRuntimeTraceEvent childLine =
        executionEvent(
            6,
            StoryGraphRuntimeTraceEventType::
                SourceLine,
            2);
    childLine.line = 1;
    events.append(childLine);
    StoryGraphRuntimeTraceEvent childCall =
        executionEvent(
            7,
            StoryGraphRuntimeTraceEventType::
                ApiCall,
            2);
    childCall.apiName = QStringLiteral("talk");
    events.append(childCall);
    StoryGraphRuntimeTraceEvent childFinish =
        executionEvent(
            8,
            StoryGraphRuntimeTraceEventType::
                ScriptFinish,
            2);
    childFinish.status = QStringLiteral("completed");
    events.append(childFinish);
    StoryGraphRuntimeTraceEvent parentFinish =
        executionEvent(
            9,
            StoryGraphRuntimeTraceEventType::
                ScriptFinish,
            1);
    parentFinish.status = QStringLiteral("completed");
    events.append(parentFinish);
    StoryGraphRuntimeTraceEvent sessionFinish =
        event(
            10,
            StoryGraphRuntimeTraceEventType::
                SessionFinish);
    sessionFinish.status =
        QStringLiteral("completed");
    events.append(sessionFinish);
    return events;
}

bool testUniqueNodesAndExistingEdges()
{
    StoryGraphTraceMatcher matcher;
    matcher.setProjectResult(makeProject());
    matcher.resetTrace(SessionId);
    bool passed = true;
    passed &= check(
        matcher.analysisGeneration() == 42 &&
        matcher.result().analysisGeneration == 42,
        "resetTrace preserves the indexed project generation");
    passed &= check(
        matcher.appendEvents(completeTrace()),
        "a nested complete execution trace is accepted");
    const StoryGraphTraceMatchResult& result =
        matcher.result();
    passed &= check(
        matcher.lastSequence() == 10 &&
        result.sessionId == SessionId &&
        result.matchedEventCount() == 8 &&
        result.unmatchedEventCount() == 0 &&
        result.ambiguousEventCount() == 0 &&
        result.staleEventCount() == 0,
        "relevant runtime events uniquely match both static graphs");
    passed &= check(
        result.controlFlow.executedNodeIds.contains(
            QStringLiteral("c-parent-line")) &&
        result.controlFlow.executedNodeIds.contains(
            QStringLiteral("c-child-line")) &&
        result.storySemantics.executedNodeIds.contains(
            QStringLiteral("s-parent-call")) &&
        result.storySemantics.executedNodeIds.contains(
            QStringLiteral("s-child-talk")),
        "only named existing nodes enter the overlays");
    const auto hasContiguousOverlayDeltas =
        [](const StoryGraphTraceGraphOverlay& overlay)
        {
            qsizetype addedNodeCount = 0;
            qsizetype addedEdgeCount = 0;
            for (qsizetype index = 0;
                 index < overlay.deltas.size();
                 ++index)
            {
                const StoryGraphTraceGraphOverlay::Delta&
                    delta = overlay.deltas.at(index);
                if (delta.revision !=
                    static_cast<quint64>(index) + 1)
                {
                    return false;
                }
                addedNodeCount +=
                    delta.addedNodeIds.size();
                addedEdgeCount +=
                    delta.addedEdgeIds.size();
            }
            return overlay.revision ==
                    static_cast<quint64>(
                        overlay.deltas.size()) &&
                addedNodeCount ==
                    overlay.executedNodeIds.size() &&
                addedEdgeCount ==
                    overlay.executedEdgeIds.size();
        };
    passed &= check(
        result.controlFlow.epoch > 0 &&
            result.storySemantics.epoch ==
                result.controlFlow.epoch &&
            hasContiguousOverlayDeltas(
                result.controlFlow) &&
            hasContiguousOverlayDeltas(
                result.storySemantics),
        "published overlay revisions contain only contiguous newly executed node and edge deltas");
    passed &= check(
        result.controlFlow.executedEdgeIds.contains(
            QStringLiteral("c-parent-entry-line")) &&
        result.controlFlow.executedEdgeIds.contains(
            QStringLiteral("c-child-line-exit")) &&
        result.storySemantics.executedEdgeIds.contains(
            QStringLiteral("s-call-child-entry")) &&
        result.storySemantics.executedEdgeIds.contains(
            QStringLiteral("s-child-talk-exit")),
        "same-execution and parent-child transitions use existing static edges");
    return passed;
}

bool testAmbiguousStaleAndNoGuessEvents()
{
    bool passed = true;

    {
        StoryGraphProjectResult project = makeProject();
        StoryGraphNode duplicate =
            project.semanticGraph.nodes.at(1);
        duplicate.id =
            QStringLiteral("s-parent-call-duplicate");
        project.semanticGraph.nodes.append(duplicate);
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(project);
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events =
            completeTrace().mid(0, 4);
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().eventMatches.last().
                storySemantics.status ==
                StoryGraphTraceMatchStatus::Ambiguous &&
            !matcher.result().storySemantics.
                executedNodeIds.contains(
                    QStringLiteral(
                        "s-parent-call-duplicate")),
            "multiple compatible nodes are reported ambiguous and never guessed");
    }

    {
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(makeProject());
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events;
        events.append(
            event(
                1,
                StoryGraphRuntimeTraceEventType::
                    SessionStart));
        events.append(
            scriptStartEvent(
                2,
                1,
                QStringLiteral("script/parent.txt"),
                std::nullopt,
                QByteArray(32, 'b')));
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().eventMatches.last().
                controlFlow.status ==
                StoryGraphTraceMatchStatus::Stale &&
            matcher.result().eventMatches.last().
                storySemantics.status ==
                StoryGraphTraceMatchStatus::Stale,
            "a content digest mismatch is stale rather than guessed");
    }

    {
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(makeProject());
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events;
        events.append(
            event(
                1,
                StoryGraphRuntimeTraceEventType::
                    SessionStart));
        StoryGraphRuntimeTraceEvent map =
            event(
                2,
                StoryGraphRuntimeTraceEventType::
                    MapChange);
        map.target =
            QStringLiteral("map/start");
        events.append(map);
        StoryGraphRuntimeTraceEvent variable =
            event(
                3,
                StoryGraphRuntimeTraceEventType::
                    VariableChange);
        variable.variableName =
            QStringLiteral("score");
        events.append(variable);
        StoryGraphRuntimeTraceEvent finish =
            event(
                4,
                StoryGraphRuntimeTraceEventType::
                    SessionFinish);
        events.append(finish);
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().eventMatches.at(1).
                storySemantics.status ==
                StoryGraphTraceMatchStatus::
                    NotApplicable &&
            matcher.result().eventMatches.at(2).
                storySemantics.status ==
                StoryGraphTraceMatchStatus::
                    NotApplicable,
            "session-level map and variable events do not guess an execution source");
    }
    return passed;
}

QVector<StoryGraphRuntimeTraceEvent>
tracePrefixForParentLine()
{
    QVector<StoryGraphRuntimeTraceEvent> events;
    events.append(
        event(
            1,
            StoryGraphRuntimeTraceEventType::
                SessionStart));
    events.append(
        scriptStartEvent(
            2,
            1,
            QStringLiteral("script/parent.txt")));
    StoryGraphRuntimeTraceEvent line =
        executionEvent(
            3,
            StoryGraphRuntimeTraceEventType::
                SourceLine,
            1);
    line.line = 2;
    events.append(line);
    return events;
}

bool testMapTargetNormalizationAndCase()
{
    StoryGraphProjectResult project = makeProject();
    StoryGraphNode mapNode =
        node(
            QStringLiteral("s-parent-map"),
            StoryGraphNodeKind::MapLoad,
            sourceIdentity(
                QStringLiteral(
                    "script/parent.txt")),
            2);
    mapNode.apiName = QStringLiteral("loadmap");
    mapNode.literalTarget =
        QStringLiteral("Town\\Gate.MAP");
    project.semanticGraph.nodes.append(mapNode);

    bool passed = true;
    {
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(project);
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events =
            tracePrefixForParentLine();
        StoryGraphRuntimeTraceEvent api =
            executionEvent(
                4,
                StoryGraphRuntimeTraceEventType::
                    ApiCall,
                1);
        api.apiName = QStringLiteral("loadmap");
        events.append(api);
        StoryGraphRuntimeTraceEvent map =
            executionEvent(
                5,
                StoryGraphRuntimeTraceEventType::
                    MapChange,
                1);
        map.target =
            QStringLiteral("Town/Gate.MAP");
        events.append(map);
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().eventMatches.at(3).
                storySemantics.status ==
                StoryGraphTraceMatchStatus::Matched &&
            matcher.result().eventMatches.at(3).
                storySemantics.nodeId ==
                QStringLiteral("s-parent-map") &&
            matcher.result().eventMatches.at(4).
                storySemantics.status ==
                StoryGraphTraceMatchStatus::Matched &&
            matcher.result().eventMatches.at(4).
                storySemantics.nodeId ==
                QStringLiteral("s-parent-map") &&
            matcher.result().storySemantics.
                executedNodeIds ==
                QSet<QString>{
                    QStringLiteral("s-parent-entry"),
                    QStringLiteral("s-parent-map")} &&
            matcher.result().storySemantics.
                executedEdgeIds.isEmpty(),
            "API and successful map change match one existing node after slash-only strict normalization");
    }

    {
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(project);
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events =
            tracePrefixForParentLine();
        StoryGraphRuntimeTraceEvent map =
            executionEvent(
                4,
                StoryGraphRuntimeTraceEventType::
                    MapChange,
                1);
        map.target =
            QStringLiteral("town/Gate.MAP");
        events.append(map);
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().eventMatches.constLast().
                storySemantics.status ==
                StoryGraphTraceMatchStatus::Unmatched &&
            !matcher.result().storySemantics.
                executedNodeIds.contains(
                    QStringLiteral("s-parent-map")),
            "map target case differences remain unmatched rather than guessed");
    }

    {
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(project);
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events;
        events.append(
            event(
                1,
                StoryGraphRuntimeTraceEventType::
                    SessionStart));
        events.append(
            scriptStartEvent(
                2,
                1,
                QStringLiteral(
                    "Script/parent.txt")));
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().eventMatches.constLast().
                storySemantics.status ==
                StoryGraphTraceMatchStatus::Unmatched,
            "source virtual paths retain exact case matching");
    }
    return passed;
}

bool testVariableAsciiCaseFoldAndAmbiguity()
{
    StoryGraphProjectResult project = makeProject();
    StoryGraphNode variableNode =
        node(
            QStringLiteral(
                "s-parent-uppercase-variable"),
            StoryGraphNodeKind::VariableWrite,
            sourceIdentity(
                QStringLiteral(
                    "script/parent.txt")),
            2);
    variableNode.apiName =
        QStringLiteral("assign");
    variableNode.variableName =
        QStringLiteral("$PlayerScore");
    project.semanticGraph.nodes.append(
        variableNode);

    bool passed = true;
    {
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(project);
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events =
            tracePrefixForParentLine();
        StoryGraphRuntimeTraceEvent api =
            executionEvent(
                4,
                StoryGraphRuntimeTraceEventType::
                    ApiCall,
                1);
        api.apiName = QStringLiteral("assign");
        events.append(api);
        StoryGraphRuntimeTraceEvent variable =
            executionEvent(
                5,
                StoryGraphRuntimeTraceEventType::
                    VariableChange,
                1);
        variable.variableName =
            QStringLiteral("$playerscore");
        events.append(variable);
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().eventMatches.at(3).
                storySemantics.status ==
                StoryGraphTraceMatchStatus::Matched &&
            matcher.result().eventMatches.at(4).
                storySemantics.status ==
                StoryGraphTraceMatchStatus::Matched &&
            matcher.result().eventMatches.at(4).
                storySemantics.nodeId ==
                QStringLiteral(
                    "s-parent-uppercase-variable"),
            "runtime lowercase variable names match one uppercase static literal by ASCII case fold");
    }

    {
        StoryGraphProjectResult ambiguousProject =
            project;
        StoryGraphNode duplicate = variableNode;
        duplicate.id =
            QStringLiteral(
                "s-parent-uppercase-variable-duplicate");
        duplicate.variableName =
            QStringLiteral("$PLAYERSCORE");
        ambiguousProject.semanticGraph.nodes.append(
            duplicate);

        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(ambiguousProject);
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events =
            tracePrefixForParentLine();
        StoryGraphRuntimeTraceEvent variable =
            executionEvent(
                4,
                StoryGraphRuntimeTraceEventType::
                    VariableChange,
                1);
        variable.variableName =
            QStringLiteral("$playerscore");
        events.append(variable);
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().eventMatches.constLast().
                storySemantics.status ==
                StoryGraphTraceMatchStatus::Ambiguous &&
            !matcher.result().storySemantics.
                executedNodeIds.contains(
                    QStringLiteral(
                        "s-parent-uppercase-variable")) &&
            !matcher.result().storySemantics.
                executedNodeIds.contains(
                    QStringLiteral(
                        "s-parent-uppercase-variable-duplicate")),
            "ASCII-fold-equivalent variable nodes remain ambiguous and are never guessed");
    }
    return passed;
}

bool testFailedScriptFinishDoesNotProveExit()
{
    bool passed = true;
    {
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(makeProject());
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events =
            tracePrefixForParentLine();
        StoryGraphRuntimeTraceEvent finish =
            executionEvent(
                4,
                StoryGraphRuntimeTraceEventType::
                    ScriptFinish,
                1);
        finish.status =
            QStringLiteral("load-error");
        events.append(finish);
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().eventMatches.constLast().
                controlFlow.status ==
                StoryGraphTraceMatchStatus::
                    NotApplicable &&
            matcher.result().eventMatches.constLast().
                storySemantics.status ==
                StoryGraphTraceMatchStatus::
                    NotApplicable &&
            !matcher.result().controlFlow.
                executedNodeIds.contains(
                    QStringLiteral("c-parent-exit")) &&
            !matcher.result().storySemantics.
                executedNodeIds.contains(
                    QStringLiteral("s-parent-exit")) &&
            !matcher.result().controlFlow.
                executedEdgeIds.contains(
                    QStringLiteral(
                        "c-parent-line-exit")),
            "load-error finish is not a normal static exit and proves no exit edge");
    }

    {
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(makeProject());
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events =
            tracePrefixForParentLine();
        StoryGraphRuntimeTraceEvent call =
            executionEvent(
                4,
                StoryGraphRuntimeTraceEventType::
                    ApiCall,
                1);
        call.apiName =
            QStringLiteral("runscript");
        events.append(call);
        StoryGraphRuntimeTraceEvent finish =
            executionEvent(
                5,
                StoryGraphRuntimeTraceEventType::
                    ScriptFinish,
                1);
        finish.status =
            QStringLiteral("aborted");
        events.append(finish);
        events.append(
            scriptStartEvent(
                6,
                2,
                QStringLiteral(
                    "script/child.txt"),
                1));
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().eventMatches.at(4).
                controlFlow.status ==
                StoryGraphTraceMatchStatus::
                    NotApplicable &&
            matcher.result().eventMatches.at(4).
                storySemantics.status ==
                StoryGraphTraceMatchStatus::
                    NotApplicable &&
            matcher.result().eventMatches.at(5).
                storySemantics.status ==
                StoryGraphTraceMatchStatus::Matched &&
            matcher.result().storySemantics.
                executedNodeIds.contains(
                    QStringLiteral("s-child-entry")) &&
            !matcher.result().storySemantics.
                executedNodeIds.contains(
                    QStringLiteral("s-parent-exit")) &&
            !matcher.result().storySemantics.
                executedEdgeIds.contains(
                    QStringLiteral(
                        "s-parent-call-exit")) &&
            !matcher.result().storySemantics.
                executedEdgeIds.contains(
                    QStringLiteral(
                        "s-call-child-entry")),
            "aborted finish breaks pending transitions before a later child start");
    }
    return passed;
}

bool testEdgeProofAndTransitionBreaks()
{
    bool passed = true;

    {
        StoryGraphProjectResult project = makeProject();
        project.controlFlowGraph.edges.erase(
            std::remove_if(
                project.controlFlowGraph.edges.begin(),
                project.controlFlowGraph.edges.end(),
                [](const StoryGraphEdge& candidate)
                {
                    return candidate.id ==
                        QStringLiteral(
                            "c-parent-entry-line");
                }),
            project.controlFlowGraph.edges.end());
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(project);
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events =
            completeTrace().mid(0, 3);
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().controlFlow.
                executedNodeIds.contains(
                    QStringLiteral("c-parent-entry")) &&
            matcher.result().controlFlow.
                executedNodeIds.contains(
                    QStringLiteral("c-parent-line")) &&
            matcher.result().controlFlow.
                executedEdgeIds.isEmpty(),
            "matched nodes never synthesize a missing static edge");
    }

    {
        StoryGraphProjectResult project = makeProject();
        project.controlFlowGraph.edges.append(
            edge(
                QStringLiteral(
                    "c-parent-entry-line-parallel"),
                QStringLiteral("c-parent-entry"),
                QStringLiteral("c-parent-line")));
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(project);
        matcher.resetTrace(SessionId);
        passed &= check(
            matcher.appendEvents(
                completeTrace().mid(0, 3)) &&
            !matcher.result().controlFlow.
                executedEdgeIds.contains(
                    QStringLiteral(
                        "c-parent-entry-line")) &&
            !matcher.result().controlFlow.
                executedEdgeIds.contains(
                    QStringLiteral(
                        "c-parent-entry-line-parallel")),
            "parallel static edges remain unproved when runtime cannot distinguish them");
    }

    {
        StoryGraphTraceMatcher matcher;
        matcher.setProjectResult(makeProject());
        matcher.resetTrace(SessionId);
        QVector<StoryGraphRuntimeTraceEvent> events =
            completeTrace().mid(0, 3);
        StoryGraphRuntimeTraceEvent unmatched =
            executionEvent(
                4,
                StoryGraphRuntimeTraceEventType::
                    SourceLine,
                1);
        unmatched.line = 99;
        events.append(unmatched);
        StoryGraphRuntimeTraceEvent next =
            executionEvent(
                5,
                StoryGraphRuntimeTraceEventType::
                    SourceLine,
                1);
        next.line = 3;
        events.append(next);
        passed &= check(
            matcher.appendEvents(events) &&
            matcher.result().controlFlow.
                executedNodeIds.contains(
                    QStringLiteral("c-parent-next")) &&
            !matcher.result().controlFlow.
                executedEdgeIds.contains(
                    QStringLiteral(
                        "c-parent-line-next")),
            "an unmatched event breaks adjacency proof");
    }
    return passed;
}

bool testDiscardAndGenerationRefresh()
{
    StoryGraphTraceMatcher matcher;
    matcher.setProjectResult(makeProject());
    matcher.resetTrace(SessionId);
    QVector<StoryGraphRuntimeTraceEvent> prefix =
        completeTrace().mid(0, 3);
    bool passed = true;
    passed &= check(
        matcher.appendEvents(prefix) &&
        matcher.discardMatchesRetainingExecutionState() ==
            3 &&
        matcher.result().eventMatches.isEmpty() &&
        matcher.result().controlFlow.
            executedNodeIds.isEmpty(),
        "discard clears published matches while retaining sequence");

    StoryGraphRuntimeTraceEvent api =
        executionEvent(
            4,
            StoryGraphRuntimeTraceEventType::ApiCall,
            1);
    api.apiName = QStringLiteral("runscript");
    passed &= check(
        matcher.appendEvent(api) &&
        matcher.result().eventMatches.constLast().
            storySemantics.status ==
            StoryGraphTraceMatchStatus::Matched &&
        matcher.result().storySemantics.
            executedNodeIds.contains(
                QStringLiteral("s-parent-call")) &&
        matcher.result().storySemantics.
            executedEdgeIds.isEmpty(),
        "N+1 uses retained source and line context without bridging to discarded nodes");

    matcher.clearProjectResult();
    matcher.setProjectResult(makeProject(43));
    passed &= check(
        matcher.hasDiscardExecutionCheckpoint() &&
        matcher.discardExecutionCheckpointSequence() ==
            3 &&
        matcher.restoreDiscardExecutionCheckpoint() &&
        matcher.lastSequence() == 3 &&
        matcher.appendEvent(api) &&
        matcher.result().eventMatches.size() == 1 &&
        matcher.result().eventMatches.constFirst().
            sequence == 4 &&
        matcher.result().eventMatches.constFirst().
            storySemantics.status ==
                StoryGraphTraceMatchStatus::Matched &&
        matcher.result().storySemantics.
            executedNodeIds.contains(
                QStringLiteral("s-parent-call")),
        "a new static generation restores clear@N execution context and rematches retained post-clear events");

    StoryGraphRuntimeTraceEvent nextLine =
        executionEvent(
            5,
            StoryGraphRuntimeTraceEventType::
                SourceLine,
            1);
    nextLine.line = 3;
    passed &= check(
        matcher.lastSequence() == 4 &&
        matcher.analysisGeneration() == 43 &&
        matcher.appendEvent(nextLine) &&
        matcher.result().analysisGeneration == 43 &&
        matcher.result().eventMatches.constLast().
            controlFlow.status ==
            StoryGraphTraceMatchStatus::Matched &&
        matcher.result().controlFlow.
            executedNodeIds.contains(
                QStringLiteral("c-parent-next")) &&
        matcher.result().controlFlow.
            executedEdgeIds.isEmpty(),
        "new project indexes preserve active execution context and sequence but break old-generation edges");

    StoryGraphTraceMatcher baselineMatcher;
    baselineMatcher.setProjectResult(makeProject(51));
    baselineMatcher.resetTrace(SessionId, 10);
    baselineMatcher.
        discardMatchesRetainingExecutionState();
    baselineMatcher.resetTrace(SessionId, 10);
    passed &= check(
        baselineMatcher.analysisGeneration() == 51 &&
        baselineMatcher.lastSequence() == 10 &&
        !baselineMatcher.
            hasDiscardExecutionCheckpoint(),
        "resetTrace supports an explicit consumed-sequence baseline and clears any prior replay checkpoint");
    return passed;
}

bool testBoundedIncrementalPresentationSummary()
{
    StoryGraphTraceMatcher matcher;
    matcher.setProjectResult(makeProject());
    matcher.resetTrace(SessionId);

    constexpr quint64 DroppedSummaryCount = 700;
    QVector<StoryGraphRuntimeTraceEvent> events;
    events.reserve(
        static_cast<qsizetype>(
            DroppedSummaryCount + 1));
    events.append(
        event(
            1,
            StoryGraphRuntimeTraceEventType::
                SessionStart));
    for (quint64 sequence = 2;
         sequence <= DroppedSummaryCount + 1;
         ++sequence)
    {
        StoryGraphRuntimeTraceEvent dropped =
            event(
                sequence,
                StoryGraphRuntimeTraceEventType::
                    TraceDropped);
        dropped.droppedSourceLineCount = 1;
        events.append(std::move(dropped));
    }

    bool passed = true;
    passed &= check(
        matcher.appendEvents(events),
        "large dropped-source-line history is accepted incrementally");
    const StoryGraphTraceMatchResult& result =
        matcher.result();
    const quint64 aggregationWorkBeforeReads =
        result.aggregationWorkItemCount();
    const qsizetype matchedBeforeReads =
        result.matchedEventCount();
    for (int index = 0; index < 100; ++index)
    {
        (void)result.matchedEventCount();
        (void)result.unmatchedEventCount();
        (void)result.ambiguousEventCount();
        (void)result.staleEventCount();
    }
    passed &= check(
        result.controlFlowIssues.issueCount ==
                static_cast<qsizetype>(
                    DroppedSummaryCount) &&
            result.storySemanticsIssues.issueCount ==
                static_cast<qsizetype>(
                    DroppedSummaryCount) &&
            result.controlFlowIssues.
                    presentedIssues.size() ==
                StoryGraphTraceGraphIssueSummary::
                    MaximumPresentedIssueCount &&
            result.storySemanticsIssues.
                    presentedIssues.size() ==
                StoryGraphTraceGraphIssueSummary::
                    MaximumPresentedIssueCount,
        "each graph caches the exact issue total while retaining at most 500 UI rows");
    passed &= check(
        result.aggregationWorkItemCount() ==
                events.size() &&
            result.aggregationWorkItemCount() ==
                aggregationWorkBeforeReads &&
            result.matchedEventCount() ==
                matchedBeforeReads,
        "status count reads scan no history and every appended event performs one aggregation work item");
    return passed;
}

bool testPortableRootKey()
{
    return check(
        makeStoryGraphPortableRootKey(
            StoryGraphContentRootKind::DependencyId,
            7,
            QStringLiteral("包/A B")) ==
            QStringLiteral(
                "story-root-v1/dependency-id/7/%E5%8C%85%2FA%20B"),
        "portable root keys use shared UTF-8 percent encoding");
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;
    passed &= testUniqueNodesAndExistingEdges();
    passed &= testAmbiguousStaleAndNoGuessEvents();
    passed &= testMapTargetNormalizationAndCase();
    passed &= testVariableAsciiCaseFoldAndAmbiguity();
    passed &= testFailedScriptFinishDoesNotProveExit();
    passed &= testEdgeProofAndTransitionBreaks();
    passed &= testDiscardAndGenerationRefresh();
    passed &= testBoundedIncrementalPresentationSummary();
    passed &= testPortableRootKey();
    if (passed)
    {
        std::cout
            << "All story graph trace matcher tests passed\n";
    }
    return passed ? 0 : 1;
}
