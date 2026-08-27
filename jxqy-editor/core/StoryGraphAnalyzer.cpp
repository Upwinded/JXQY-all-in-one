#include "StoryGraphAnalyzer.h"

#include "StoryGraphRuntimeApiCatalog.h"
#include "StoryGraphSemanticCatalog.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
constexpr int ParserCancellationCheckInterval = 1024;
constexpr int ChoiceMultipleGeneratedVariableLimit = 32;

struct TokenSpan
{
    int first = -1;
    int lastExclusive = -1;

    bool isValid() const
    {
        return first >= 0 &&
            lastExclusive > first;
    }
};

struct StatementFlow
{
    QString entryNodeId;
    QList<QString> exitNodeIds;
    QList<QString> breakNodeIds;
    QList<QString> returnNodeIds;
};

struct BlockFlow
{
    QString entryNodeId;
    QList<QString> exitNodeIds;
    QList<QString> breakNodeIds;
    QList<QString> returnNodeIds;
};

struct PendingGoto
{
    QString nodeId;
    QString labelName;
    StoryGraphSourceRange sourceRange;
    int lexicalBlockId = -1;
    QSet<int> visibleLocalDeclarationIds;
};

struct LabelDefinition
{
    QString nodeId;
    QString labelName;
    int lexicalBlockId = -1;
    QSet<int> visibleLocalDeclarationIds;
};

struct LexicalBlock
{
    int parentBlockId = -1;
    QSet<int> entryLocalDeclarationIds;
    QList<int> ownedLocalDeclarationIds;
    QList<int> terminalLabelIndexes;
};

struct ParserScope
{
    QString id;
    QSet<QString> visibleLocalNames;
    QSet<QString> mutatedGlobalNames;
    QList<LexicalBlock> lexicalBlocks;
    QList<int> lexicalBlockStack;
    QSet<int> visibleLocalDeclarationIds;
    QList<LabelDefinition> labels;
    QHash<QString, int> labelIndexByBlockAndName;
    QSet<QString> duplicateLabelKeys;
    QList<PendingGoto> pendingGotos;
    int nextLocalDeclarationId = 0;
};

struct CallArguments
{
    QList<TokenSpan> arguments;
    int closingTokenIndex = -1;
    bool complete = false;
};

struct DeclarationTargets
{
    bool declaration = false;
    bool complete = false;
    QList<TokenSpan> names;
};

struct SemanticProjectionState
{
    QString controlNodeId;
    StoryGraphEdgeKind edgeKind =
        StoryGraphEdgeKind::Sequential;
    StoryGraphCertainty certainty =
        StoryGraphCertainty::Certain;
};

StoryGraphSourcePosition toStoryGraphPosition(
    const LuaSourcePosition& position)
{
    StoryGraphSourcePosition result;
    result.line = position.line;
    result.column = position.column;
    result.utf16Offset = position.utf16Offset;
    result.utf8ByteOffset = position.utf8ByteOffset;
    return result;
}

StoryGraphSourceRange toStoryGraphRange(
    const LuaSourceRange& range)
{
    StoryGraphSourceRange result;
    result.start = toStoryGraphPosition(range.start);
    result.end = toStoryGraphPosition(range.end);
    return result;
}

QString tokenKindFingerprint(
    LuaTokenKind kind)
{
    switch (kind)
    {
    case LuaTokenKind::Identifier:
        return QStringLiteral("identifier");
    case LuaTokenKind::Keyword:
        return QStringLiteral("keyword");
    case LuaTokenKind::Number:
        return QStringLiteral("number");
    case LuaTokenKind::String:
        return QStringLiteral("string");
    case LuaTokenKind::LongString:
        return QStringLiteral("long-string");
    case LuaTokenKind::Symbol:
        return QStringLiteral("symbol");
    case LuaTokenKind::Comment:
        return QStringLiteral("comment");
    case LuaTokenKind::EndOfFile:
        return QStringLiteral("end");
    }
    return QStringLiteral("invalid");
}

StoryGraphNodeKind semanticNodeKind(
    StoryGraphSemanticCategory category,
    StoryGraphScriptCallKind scriptCallKind)
{
    switch (category)
    {
    case StoryGraphSemanticCategory::VariableRead:
        return StoryGraphNodeKind::VariableRead;
    case StoryGraphSemanticCategory::VariableWrite:
        return StoryGraphNodeKind::VariableWrite;
    case StoryGraphSemanticCategory::Dialogue:
        return StoryGraphNodeKind::Dialogue;
    case StoryGraphSemanticCategory::Choice:
        return StoryGraphNodeKind::Choice;
    case StoryGraphSemanticCategory::ScriptCall:
        return scriptCallKind ==
                StoryGraphScriptCallKind::Parallel
            ? StoryGraphNodeKind::ParallelScriptCall
            : StoryGraphNodeKind::SerialScriptCall;
    case StoryGraphSemanticCategory::MapTransition:
        return StoryGraphNodeKind::MapLoad;
    case StoryGraphSemanticCategory::MapContextInvalidator:
        return StoryGraphNodeKind::RegisteredApiCall;
    case StoryGraphSemanticCategory::Combat:
        return StoryGraphNodeKind::Battle;
    }
    return StoryGraphNodeKind::RegisteredApiCall;
}

class Parser
{
public:
    Parser(
        const StoryGraphAnalysisRequest& request,
        const LuaLexResult& lexResult,
        StoryGraphDocumentResult& documentResult,
        const StoryGraphAnalyzer::CancelCallback&
            cancelCallback)
        : request(request)
        , documentResult(documentResult)
        , cancelCallback(cancelCallback)
    {
        for (const LuaToken& token : lexResult.tokens)
        {
            if (checkCancellation())
                break;
            if (LuaLexer::isSignificant(token))
                tokens.append(token);
        }

        controlGraph =
            &documentResult.controlFlowGraph;
        semanticGraph =
            &documentResult.semanticGraph;
        controlGraph->kind =
            StoryGraphKind::ControlFlow;
        semanticGraph->kind =
            StoryGraphKind::StorySemantics;
    }

    void parse()
    {
        if (cancelled || checkCancellation())
        {
            finishCancelled();
            return;
        }

        ParserScope chunkScope;
        chunkScope.id = QStringLiteral("chunk");

        const TokenSpan emptyAnchor =
            anchorSpanAt(0);
        const QString controlEntry =
            addNode(
                *controlGraph,
                StoryGraphNodeKind::ChunkEntry,
                StoryGraphCertainty::Certain,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "脚本入口"),
                QString(),
                chunkScope.id,
                QStringLiteral("chunk-entry"),
                emptyAnchor);
        const QString semanticEntry =
            addNode(
                *semanticGraph,
                StoryGraphNodeKind::ChunkEntry,
                StoryGraphCertainty::Certain,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "剧情入口"),
                QString(),
                chunkScope.id,
                QStringLiteral("story-entry"),
                emptyAnchor);
        controlGraph->entryNodeId = controlEntry;
        semanticGraph->entryNodeId = semanticEntry;
        semanticGraph->nodes.last().controlFlowNodeId =
            controlEntry;
        appendSemanticNodeForControl(
            controlEntry,
            semanticEntry);

        BlockFlow chunkFlow =
            parseBlock(
                chunkScope,
                {},
                false);
        if (cancelled)
        {
            finishCancelled();
            return;
        }

        const TokenSpan exitAnchor =
            anchorSpanAt(tokenCount());
        const QString controlExit =
            addNode(
                *controlGraph,
                StoryGraphNodeKind::ChunkExit,
                StoryGraphCertainty::Certain,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "脚本出口"),
                QString(),
                chunkScope.id,
                QStringLiteral("chunk-exit"),
                exitAnchor);
        const QString semanticExit =
            addNode(
                *semanticGraph,
                StoryGraphNodeKind::ChunkExit,
                StoryGraphCertainty::Certain,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "剧情出口"),
                QString(),
                chunkScope.id,
                QStringLiteral("story-exit"),
                exitAnchor);
        controlGraph->exitNodeId = controlExit;
        semanticGraph->exitNodeId = semanticExit;
        semanticGraph->nodes.last().controlFlowNodeId =
            controlExit;
        appendSemanticNodeForControl(
            controlExit,
            semanticExit);

        if (chunkFlow.entryNodeId.isEmpty())
        {
            addEdge(
                *controlGraph,
                controlEntry,
                controlExit,
                StoryGraphEdgeKind::Sequential,
                StoryGraphCertainty::Certain,
                QStringLiteral("empty"));
        }
        else
        {
            addEdge(
                *controlGraph,
                controlEntry,
                chunkFlow.entryNodeId,
                StoryGraphEdgeKind::Sequential,
                StoryGraphCertainty::Certain,
                QStringLiteral("entry"));
            for (const QString& exitNodeId :
                 chunkFlow.exitNodeIds)
            {
                addEdge(
                    *controlGraph,
                    exitNodeId,
                    controlExit,
                    StoryGraphEdgeKind::Fallthrough,
                    StoryGraphCertainty::Certain,
                    QStringLiteral("chunk-exit"));
            }
        }
        for (const QString& returnNodeId :
             chunkFlow.returnNodeIds)
        {
            addEdge(
                *controlGraph,
                returnNodeId,
                controlExit,
                StoryGraphEdgeKind::Return,
                StoryGraphCertainty::Certain,
                QStringLiteral("return"));
        }
        for (const QString& breakNodeId :
             chunkFlow.breakNodeIds)
        {
            addWarning(
                StoryGraphWarningCategory::ControlFlow,
                StoryGraphWarningSeverity::Warning,
                QStringLiteral(
                    "story_graph.control.break_outside_loop"),
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "break 不在可解析的循环体内"),
                rangeForNode(
                    *controlGraph,
                    breakNodeId),
                breakNodeId,
                controlGraph);
            addEdge(
                *controlGraph,
                breakNodeId,
                controlExit,
                StoryGraphEdgeKind::Unknown,
                StoryGraphCertainty::Warning,
                QStringLiteral("invalid-break"));
        }
        resolveGotos(
            chunkScope,
            controlExit);
        if (cancelled)
        {
            finishCancelled();
            return;
        }
        buildSemanticEdges();
        if (cancelled)
            finishCancelled();
    }

private:
    void finishCancelled()
    {
        documentResult.status =
            StoryGraphDocumentStatus::Cancelled;
        controlGraph->complete = false;
        semanticGraph->complete = false;
    }

    bool checkCancellation()
    {
        ++operationCount;
        if (operationCount <
            nextCancellationCheck)
        {
            return cancelled;
        }
        nextCancellationCheck =
            operationCount +
            ParserCancellationCheckInterval;
        if (cancelCallback &&
            cancelCallback())
        {
            cancelled = true;
        }
        return cancelled;
    }

    bool atEnd() const
    {
        return cursor >= tokenCount();
    }

    int tokenCount() const
    {
        return static_cast<int>(
            tokens.size());
    }

    const LuaToken* tokenAt(int index) const
    {
        if (index < 0 || index >= tokenCount())
            return nullptr;
        return &tokens.at(index);
    }

    QString textAt(int index) const
    {
        const LuaToken* token = tokenAt(index);
        return token != nullptr
            ? token->text
            : QString();
    }

    bool isTextAt(
        int index,
        const QString& text) const
    {
        return textAt(index) == text;
    }

    TokenSpan anchorSpanAt(int index) const
    {
        if (tokens.isEmpty())
            return {};
        if (index >= tokenCount())
        {
            return {
                tokenCount() - 1,
                tokenCount()};
        }
        return {std::max(0, index),
                std::min(tokenCount(), index + 1)};
    }

    StoryGraphSourceRange rangeForSpan(
        const TokenSpan& span) const
    {
        StoryGraphSourceRange range;
        if (!span.isValid() ||
            tokens.isEmpty())
        {
            return range;
        }
        const int first =
            std::clamp(
                span.first,
                0,
                tokenCount() - 1);
        const int last =
            std::clamp(
                span.lastExclusive - 1,
                first,
                tokenCount() - 1);
        range.start =
            toStoryGraphPosition(
                tokens.at(first).range.start);
        range.end =
            toStoryGraphPosition(
                tokens.at(last).range.end);
        return range;
    }

    StoryGraphSourceRange rangeForNode(
        const StoryGraphResult& graph,
        const QString& nodeId) const
    {
        const StoryGraphNode* node =
            graph.findNode(nodeId);
        return node != nullptr
            ? node->sourceRange
            : StoryGraphSourceRange();
    }

    QString fingerprintForSpan(
        const TokenSpan& span)
    {
        QCryptographicHash hash(
            QCryptographicHash::Sha256);
        const int first =
            std::max(0, span.first);
        const int last =
            std::min(
                tokenCount(),
                span.lastExclusive);
        for (int index = first;
             index < last;
             ++index)
        {
            if (checkCancellation())
                break;
            const LuaToken& token =
                tokens.at(index);
            const QString kind =
                tokenKindFingerprint(token.kind);
            const QString value =
                token.kind == LuaTokenKind::String ||
                    token.kind ==
                        LuaTokenKind::LongString
                ? token.decodedText
                : token.text;
            const QByteArray kindBytes =
                kind.toUtf8();
            const QByteArray valueBytes =
                value.toUtf8();
            hash.addData(
                QByteArray::number(
                    kindBytes.size()));
            hash.addData(":");
            hash.addData(kindBytes);
            hash.addData(
                QByteArray::number(
                    valueBytes.size()));
            hash.addData(":");
            hash.addData(valueBytes);
        }
        return QStringLiteral("tokens-v1:") +
            QString::fromLatin1(
                hash.result().toHex());
    }

    QString displayForSpan(
        const TokenSpan& span)
    {
        QString result;
        const int last =
            std::min(
                tokenCount(),
                span.lastExclusive);
        for (int index =
                 std::max(0, span.first);
             index < last;
             ++index)
        {
            if (checkCancellation())
                break;
            const QString text =
                tokens.at(index).text;
            if (!result.isEmpty())
                result.append(QLatin1Char(' '));
            if (result.size() + text.size() > 96)
            {
                result.append(
                    QStringLiteral("…"));
                break;
            }
            result.append(text);
        }
        return result;
    }

    QString addNode(
        StoryGraphResult& graph,
        StoryGraphNodeKind kind,
        StoryGraphCertainty certainty,
        const QString& title,
        const QString& summary,
        const QString& scopeId,
        const QString& semanticFingerprint,
        const TokenSpan& span)
    {
        const QString occurrenceBase =
            storyGraphKindToString(graph.kind) +
            QLatin1Char('|') +
            scopeId +
            QLatin1Char('|') +
            storyGraphNodeKindToString(kind) +
            QLatin1Char('|') +
            semanticFingerprint;
        const int occurrence =
            nodeOccurrenceCounts.value(
                occurrenceBase);
        nodeOccurrenceCounts.insert(
            occurrenceBase,
            occurrence + 1);
        const QString occurrenceKey =
            storyGraphNodeKindToString(kind) +
            QLatin1Char('#') +
            QString::number(occurrence);

        StoryGraphStableNodeIdInput idInput;
        idInput.portableRootKey =
            documentResult.source.portableRootKey;
        idInput.strictVirtualPath =
            documentResult.source.virtualPath;
        idInput.scopeId = scopeId;
        idInput.kind = kind;
        idInput.semanticFingerprint =
            semanticFingerprint;
        idInput.structuralOccurrenceKey =
            occurrenceKey;

        StoryGraphNode node;
        node.id = makeStoryGraphNodeId(idInput);
        node.kind = kind;
        node.certainty = certainty;
        node.title = title;
        node.summary = summary;
        node.source = documentResult.source;
        node.sourceRange = rangeForSpan(span);
        node.scopeId = scopeId;
        node.semanticFingerprint =
            semanticFingerprint;
        node.structuralOccurrenceKey =
            occurrenceKey;
        graph.nodes.append(std::move(node));
        return graph.nodes.constLast().id;
    }

    void addEdge(
        StoryGraphResult& graph,
        const QString& fromNodeId,
        const QString& toNodeId,
        StoryGraphEdgeKind kind,
        StoryGraphCertainty certainty,
        const QString& semanticFingerprint,
        const QString& label = QString())
    {
        if (fromNodeId.isEmpty() ||
            toNodeId.isEmpty())
        {
            return;
        }
        const QString occurrenceBase =
            storyGraphKindToString(graph.kind) +
            QLatin1Char('|') +
            fromNodeId +
            QLatin1Char('|') +
            toNodeId +
            QLatin1Char('|') +
            storyGraphEdgeKindToString(kind) +
            QLatin1Char('|') +
            semanticFingerprint;
        const int occurrence =
            edgeOccurrenceCounts.value(
                occurrenceBase);
        edgeOccurrenceCounts.insert(
            occurrenceBase,
            occurrence + 1);

        StoryGraphStableEdgeIdInput idInput;
        idInput.fromNodeId = fromNodeId;
        idInput.toNodeId = toNodeId;
        idInput.kind = kind;
        idInput.semanticFingerprint =
            semanticFingerprint;
        idInput.structuralOccurrenceKey =
            QString::number(occurrence);

        StoryGraphEdge edge;
        edge.id = makeStoryGraphEdgeId(idInput);
        edge.fromNodeId = fromNodeId;
        edge.toNodeId = toNodeId;
        edge.kind = kind;
        edge.certainty = certainty;
        edge.label = label;
        edge.semanticFingerprint =
            semanticFingerprint;
        edge.structuralOccurrenceKey =
            idInput.structuralOccurrenceKey;
        graph.edges.append(std::move(edge));
    }

    void addWarning(
        StoryGraphWarningCategory category,
        StoryGraphWarningSeverity severity,
        const QString& diagnosticCode,
        const QString& message,
        const StoryGraphSourceRange& sourceRange,
        const QString& relatedNodeId,
        StoryGraphResult* graph)
    {
        StoryGraphWarning warning;
        warning.category = category;
        warning.severity = severity;
        warning.diagnosticCode = diagnosticCode;
        warning.message = message;
        warning.source = documentResult.source;
        warning.sourceRange = sourceRange;
        warning.relatedNodeId = relatedNodeId;
        documentResult.warnings.append(warning);
        if (graph != nullptr)
            graph->warnings.append(std::move(warning));
        if (severity !=
            StoryGraphWarningSeverity::Information)
        {
            documentResult.status =
                StoryGraphDocumentStatus::Partial;
        }
    }

    QString addControlNode(
        StoryGraphNodeKind kind,
        const QString& title,
        const TokenSpan& span,
        ParserScope& scope,
        StoryGraphCertainty certainty =
            StoryGraphCertainty::Certain,
        bool extractSemanticCalls = true)
    {
        const QString fingerprint =
            fingerprintForSpan(span);
        const QString nodeId =
            addNode(
                *controlGraph,
                kind,
                certainty,
                title,
                displayForSpan(span),
                scope.id,
                fingerprint,
                span);
        if (extractSemanticCalls)
        {
            extractCalls(
                span,
                scope,
                nodeId);
        }
        return nodeId;
    }

    void connectPrevious(
        QList<QString>& previousExitNodeIds,
        const QString& nextEntryNodeId)
    {
        for (const QString& previousNodeId :
             previousExitNodeIds)
        {
            addEdge(
                *controlGraph,
                previousNodeId,
                nextEntryNodeId,
                StoryGraphEdgeKind::Sequential,
                StoryGraphCertainty::Certain,
                QStringLiteral("sequence"));
        }
    }

    QString scopedLabelKey(
        int lexicalBlockId,
        const QString& labelName) const
    {
        return QString::number(lexicalBlockId) +
            QLatin1Char(':') +
            labelName;
    }

    int currentLexicalBlockId(
        const ParserScope& scope) const
    {
        if (scope.lexicalBlockStack.isEmpty())
            return -1;
        return scope.lexicalBlockStack.constLast();
    }

    void registerLocalDeclaration(
        ParserScope& scope)
    {
        const int lexicalBlockId =
            currentLexicalBlockId(scope);
        if (lexicalBlockId < 0 ||
            lexicalBlockId >=
                scope.lexicalBlocks.size())
        {
            return;
        }
        const int declarationId =
            scope.nextLocalDeclarationId++;
        scope.visibleLocalDeclarationIds.insert(
            declarationId);
        scope.lexicalBlocks[
            lexicalBlockId].
                ownedLocalDeclarationIds.append(
                    declarationId);
    }

    int enterLexicalBlock(
        ParserScope& scope,
        const QList<QString>& initialLocalNames)
    {
        LexicalBlock block;
        block.parentBlockId =
            currentLexicalBlockId(scope);
        const int lexicalBlockId =
            scope.lexicalBlocks.size();
        scope.lexicalBlocks.append(
            std::move(block));
        scope.lexicalBlockStack.append(
            lexicalBlockId);
        for (const QString& localName :
             initialLocalNames)
        {
            Q_UNUSED(localName);
            if (checkCancellation())
                break;
            registerLocalDeclaration(scope);
        }
        scope.lexicalBlocks[
            lexicalBlockId].
                entryLocalDeclarationIds =
                    scope.visibleLocalDeclarationIds;
        return lexicalBlockId;
    }

    void leaveLexicalBlock(
        ParserScope& scope,
        int lexicalBlockId)
    {
        if (lexicalBlockId < 0 ||
            lexicalBlockId >=
                scope.lexicalBlocks.size())
        {
            return;
        }
        const LexicalBlock& block =
            scope.lexicalBlocks.at(
                lexicalBlockId);
        for (int labelIndex :
             block.terminalLabelIndexes)
        {
            if (labelIndex >= 0 &&
                labelIndex < scope.labels.size())
            {
                scope.labels[
                    labelIndex].
                        visibleLocalDeclarationIds =
                            block.
                                entryLocalDeclarationIds;
            }
        }
        for (int declarationId :
             block.ownedLocalDeclarationIds)
        {
            scope.visibleLocalDeclarationIds.remove(
                declarationId);
        }
        if (!scope.lexicalBlockStack.isEmpty() &&
            scope.lexicalBlockStack.constLast() ==
                lexicalBlockId)
        {
            scope.lexicalBlockStack.removeLast();
        }
    }

    void clearTerminalLabels(
        ParserScope& scope)
    {
        const int lexicalBlockId =
            currentLexicalBlockId(scope);
        if (lexicalBlockId < 0 ||
            lexicalBlockId >=
                scope.lexicalBlocks.size())
        {
            return;
        }
        scope.lexicalBlocks[
            lexicalBlockId].
                terminalLabelIndexes.clear();
    }

    BlockFlow parseBlock(
        ParserScope& scope,
        const QSet<QString>& stopWords,
        bool lexicalScope,
        const QList<QString>& initialLocalNames =
            QList<QString>())
    {
        const QSet<QString> savedLocalNames =
            scope.visibleLocalNames;
        const int lexicalBlockId =
            enterLexicalBlock(
                scope,
                initialLocalNames);
        BlockFlow result;
        QList<QString> previousExitNodeIds;

        while (!atEnd() &&
               !stopWords.contains(
                   textAt(cursor)))
        {
            if (checkCancellation())
                break;
            const int before = cursor;
            if (!isTextAt(
                    cursor,
                    QStringLiteral("::")) &&
                !isTextAt(
                    cursor,
                    QStringLiteral(";")))
            {
                clearTerminalLabels(scope);
            }
            StatementFlow statement =
                parseStatement(
                    scope,
                    stopWords);
            if (cursor <= before)
            {
                const TokenSpan recoverySpan{
                    cursor,
                    std::min(
                        cursor + 1,
                        tokenCount())};
                const QString recoveryNode =
                    addControlNode(
                        StoryGraphNodeKind::Warning,
                        QCoreApplication::translate(
                            "StoryGraphAnalyzer",
                            "未解析 token"),
                        recoverySpan,
                        scope,
                        StoryGraphCertainty::Warning);
                addWarning(
                    StoryGraphWarningCategory::Syntax,
                    StoryGraphWarningSeverity::Warning,
                    QStringLiteral(
                        "story_graph.parser.no_progress"),
                    QCoreApplication::translate(
                        "StoryGraphAnalyzer",
                        "解析器已跳过无法消费的 token"),
                    rangeForSpan(recoverySpan),
                    recoveryNode,
                    controlGraph);
                ++cursor;
                statement.entryNodeId =
                    recoveryNode;
                statement.exitNodeIds = {
                    recoveryNode};
            }

            if (statement.entryNodeId.isEmpty())
                continue;
            if (result.entryNodeId.isEmpty())
            {
                result.entryNodeId =
                    statement.entryNodeId;
            }
            connectPrevious(
                previousExitNodeIds,
                statement.entryNodeId);
            previousExitNodeIds =
                statement.exitNodeIds;
            result.breakNodeIds.append(
                statement.breakNodeIds);
            result.returnNodeIds.append(
                statement.returnNodeIds);
        }

        result.exitNodeIds =
            previousExitNodeIds;
        if (lexicalScope)
        {
            scope.visibleLocalNames =
                savedLocalNames;
        }
        leaveLexicalBlock(
            scope,
            lexicalBlockId);
        return result;
    }

    StatementFlow parseStatement(
        ParserScope& scope,
        const QSet<QString>& stopWords)
    {
        if (atEnd())
            return {};
        if (isTextAt(
                cursor,
                QStringLiteral(";")))
        {
            ++cursor;
            return {};
        }
        if (isTextAt(
                cursor,
                QStringLiteral("::")))
        {
            return parseLabel(scope);
        }

        const QString text =
            textAt(cursor);
        if (text == QStringLiteral("if"))
            return parseIf(scope);
        if (text == QStringLiteral("while"))
            return parseWhile(scope);
        if (text == QStringLiteral("for"))
            return parseFor(scope);
        if (text == QStringLiteral("repeat"))
            return parseRepeat(scope);
        if (text == QStringLiteral("do"))
            return parseDo(scope);
        if (text == QStringLiteral("return"))
            return parseReturn(scope, stopWords);
        if (text == QStringLiteral("break"))
            return parseBreak(scope);
        if (text == QStringLiteral("goto"))
            return parseGoto(scope);
        if (isFunctionDefinitionStart(cursor))
            return parseFunctionDefinition(scope);
        return parseSimpleStatement(
            scope,
            stopWords);
    }

    bool isFunctionDefinitionStart(
        int index) const
    {
        if (isTextAt(
                index,
                QStringLiteral("function")))
        {
            return true;
        }
        if ((isTextAt(
                 index,
                 QStringLiteral("local")) ||
             isTextAt(
                 index,
                 QStringLiteral("global"))) &&
            isTextAt(
                index + 1,
                QStringLiteral("function")))
        {
            return true;
        }
        return false;
    }

    int consumeHeaderThrough(
        const QString& terminalWord)
    {
        int parenthesisDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;
        while (!atEnd())
        {
            const QString text =
                textAt(cursor);
            if (parenthesisDepth == 0 &&
                bracketDepth == 0 &&
                braceDepth == 0 &&
                text == terminalWord)
            {
                ++cursor;
                return cursor;
            }
            if (text == QStringLiteral("("))
                ++parenthesisDepth;
            else if (text == QStringLiteral(")"))
                parenthesisDepth =
                    std::max(0, parenthesisDepth - 1);
            else if (text == QStringLiteral("["))
                ++bracketDepth;
            else if (text == QStringLiteral("]"))
                bracketDepth =
                    std::max(0, bracketDepth - 1);
            else if (text == QStringLiteral("{"))
                ++braceDepth;
            else if (text == QStringLiteral("}"))
                braceDepth =
                    std::max(0, braceDepth - 1);
            ++cursor;
            if (checkCancellation())
                break;
        }
        return -1;
    }

    StatementFlow parseIf(
        ParserScope& scope)
    {
        struct ConditionalArm
        {
            QString conditionNodeId;
            BlockFlow block;
        };

        QList<ConditionalArm> arms;
        QList<QString> breakNodeIds;
        QList<QString> returnNodeIds;
        int conditionStart = cursor;
        ++cursor;
        if (consumeHeaderThrough(
                QStringLiteral("then")) < 0)
        {
            const TokenSpan span{
                conditionStart,
                std::max(
                    conditionStart + 1,
                    cursor)};
            const QString nodeId =
                addControlNode(
                    StoryGraphNodeKind::Condition,
                    QCoreApplication::translate(
                        "StoryGraphAnalyzer",
                        "不完整的 if"),
                    span,
                    scope,
                    StoryGraphCertainty::Warning);
            addWarning(
                StoryGraphWarningCategory::Syntax,
                StoryGraphWarningSeverity::Warning,
                QStringLiteral(
                    "story_graph.parser.missing_then"),
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "if 条件缺少 then"),
                rangeForSpan(span),
                nodeId,
                controlGraph);
            return {
                nodeId,
                {nodeId},
                {},
                {}};
        }

        ConditionalArm firstArm;
        firstArm.conditionNodeId =
            addControlNode(
                StoryGraphNodeKind::Condition,
                QStringLiteral("if"),
                {conditionStart, cursor},
                scope);
        firstArm.block =
            parseBlock(
                scope,
                {
                    QStringLiteral("elseif"),
                    QStringLiteral("else"),
                    QStringLiteral("end")
                },
                true);
        arms.append(firstArm);

        while (isTextAt(
            cursor,
            QStringLiteral("elseif")))
        {
            conditionStart = cursor;
            ++cursor;
            if (consumeHeaderThrough(
                    QStringLiteral("then")) < 0)
            {
                break;
            }
            ConditionalArm arm;
            arm.conditionNodeId =
                addControlNode(
                    StoryGraphNodeKind::Condition,
                    QStringLiteral("elseif"),
                    {conditionStart, cursor},
                    scope);
            arm.block =
                parseBlock(
                    scope,
                    {
                        QStringLiteral("elseif"),
                        QStringLiteral("else"),
                        QStringLiteral("end")
                    },
                    true);
            arms.append(arm);
        }

        bool hasElse = false;
        BlockFlow elseBlock;
        if (isTextAt(
                cursor,
                QStringLiteral("else")))
        {
            hasElse = true;
            ++cursor;
            elseBlock =
                parseBlock(
                    scope,
                    {QStringLiteral("end")},
                    true);
        }

        const int endIndex = cursor;
        if (isTextAt(
                cursor,
                QStringLiteral("end")))
        {
            ++cursor;
        }
        else
        {
            addWarning(
                StoryGraphWarningCategory::Syntax,
                StoryGraphWarningSeverity::Warning,
                QStringLiteral(
                    "story_graph.parser.missing_end"),
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "if 语句缺少 end"),
                rangeForSpan(
                    anchorSpanAt(endIndex)),
                arms.constLast().conditionNodeId,
                controlGraph);
        }

        const QString mergeNodeId =
            addControlNode(
                StoryGraphNodeKind::Merge,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "if 汇合"),
                anchorSpanAt(endIndex),
                scope,
                StoryGraphCertainty::Certain,
                false);
        bool hasFallthrough = false;

        for (int index = 0;
             index < arms.size();
             ++index)
        {
            const ConditionalArm& arm =
                arms.at(index);
            if (arm.block.entryNodeId.isEmpty())
            {
                addEdge(
                    *controlGraph,
                    arm.conditionNodeId,
                    mergeNodeId,
                    StoryGraphEdgeKind::TrueBranch,
                    StoryGraphCertainty::Certain,
                    QStringLiteral("if-empty-true"));
                hasFallthrough = true;
            }
            else
            {
                addEdge(
                    *controlGraph,
                    arm.conditionNodeId,
                    arm.block.entryNodeId,
                    StoryGraphEdgeKind::TrueBranch,
                    StoryGraphCertainty::Certain,
                    QStringLiteral("if-true"));
                for (const QString& exitNodeId :
                     arm.block.exitNodeIds)
                {
                    addEdge(
                        *controlGraph,
                        exitNodeId,
                        mergeNodeId,
                        StoryGraphEdgeKind::Fallthrough,
                        StoryGraphCertainty::Certain,
                        QStringLiteral(
                            "if-arm-exit"));
                    hasFallthrough = true;
                }
            }
            breakNodeIds.append(
                arm.block.breakNodeIds);
            returnNodeIds.append(
                arm.block.returnNodeIds);

            if (index + 1 < arms.size())
            {
                addEdge(
                    *controlGraph,
                    arm.conditionNodeId,
                    arms.at(index + 1)
                        .conditionNodeId,
                    StoryGraphEdgeKind::FalseBranch,
                    StoryGraphCertainty::Certain,
                    QStringLiteral("elseif"));
            }
        }

        const QString lastConditionNodeId =
            arms.constLast().conditionNodeId;
        if (!hasElse)
        {
            addEdge(
                *controlGraph,
                lastConditionNodeId,
                mergeNodeId,
                StoryGraphEdgeKind::FalseBranch,
                StoryGraphCertainty::Certain,
                QStringLiteral("if-false"));
            hasFallthrough = true;
        }
        else if (elseBlock.entryNodeId.isEmpty())
        {
            addEdge(
                *controlGraph,
                lastConditionNodeId,
                mergeNodeId,
                StoryGraphEdgeKind::FalseBranch,
                StoryGraphCertainty::Certain,
                QStringLiteral("else-empty"));
            hasFallthrough = true;
        }
        else
        {
            addEdge(
                *controlGraph,
                lastConditionNodeId,
                elseBlock.entryNodeId,
                StoryGraphEdgeKind::FalseBranch,
                StoryGraphCertainty::Certain,
                QStringLiteral("else"));
            for (const QString& exitNodeId :
                 elseBlock.exitNodeIds)
            {
                addEdge(
                    *controlGraph,
                    exitNodeId,
                    mergeNodeId,
                    StoryGraphEdgeKind::Fallthrough,
                    StoryGraphCertainty::Certain,
                    QStringLiteral("else-exit"));
                hasFallthrough = true;
            }
            breakNodeIds.append(
                elseBlock.breakNodeIds);
            returnNodeIds.append(
                elseBlock.returnNodeIds);
        }

        StatementFlow result;
        result.entryNodeId =
            arms.first().conditionNodeId;
        if (hasFallthrough)
        {
            result.exitNodeIds = {
                mergeNodeId};
        }
        result.breakNodeIds =
            breakNodeIds;
        result.returnNodeIds =
            returnNodeIds;
        return result;
    }

    StatementFlow parseWhile(
        ParserScope& scope)
    {
        const int start = cursor;
        ++cursor;
        if (consumeHeaderThrough(
                QStringLiteral("do")) < 0)
        {
            return parseIncompleteControl(
                scope,
                start,
                QStringLiteral("while"),
                QStringLiteral(
                    "story_graph.parser.missing_do"));
        }
        const QString conditionNodeId =
            addControlNode(
                StoryGraphNodeKind::LoopHeader,
                QStringLiteral("while"),
                {start, cursor},
                scope);
        BlockFlow body =
            parseBlock(
                scope,
                {QStringLiteral("end")},
                true);
        const int endIndex = cursor;
        consumeExpectedEnd(
            scope,
            conditionNodeId,
            QStringLiteral("while"));
        const QString mergeNodeId =
            addControlNode(
                StoryGraphNodeKind::Merge,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "while 出口"),
                anchorSpanAt(endIndex),
                scope,
                StoryGraphCertainty::Certain,
                false);
        if (body.entryNodeId.isEmpty())
        {
            addEdge(
                *controlGraph,
                conditionNodeId,
                conditionNodeId,
                StoryGraphEdgeKind::LoopBack,
                StoryGraphCertainty::Certain,
                QStringLiteral("empty-while-body"));
        }
        else
        {
            addEdge(
                *controlGraph,
                conditionNodeId,
                body.entryNodeId,
                StoryGraphEdgeKind::LoopBody,
                StoryGraphCertainty::Certain,
                QStringLiteral("while-true"));
            for (const QString& exitNodeId :
                 body.exitNodeIds)
            {
                addEdge(
                    *controlGraph,
                    exitNodeId,
                    conditionNodeId,
                    StoryGraphEdgeKind::LoopBack,
                    StoryGraphCertainty::Certain,
                    QStringLiteral("while-back"));
            }
        }
        addEdge(
            *controlGraph,
            conditionNodeId,
            mergeNodeId,
            StoryGraphEdgeKind::FalseBranch,
            StoryGraphCertainty::Certain,
            QStringLiteral("while-false"));
        connectBreaksToMerge(
            body.breakNodeIds,
            mergeNodeId);
        return {
            conditionNodeId,
            {mergeNodeId},
            {},
            body.returnNodeIds};
    }

    QSet<QString> forControlVariableNames(
        int start,
        int headerEnd)
    {
        QSet<QString> names;
        bool expectName = true;
        for (int index = start + 1;
             index < headerEnd;
             ++index)
        {
            if (checkCancellation())
                break;
            const QString text = textAt(index);
            if (text == QStringLiteral("=") ||
                text == QStringLiteral("in"))
            {
                break;
            }
            if (text == QStringLiteral(","))
            {
                expectName = true;
                continue;
            }
            const LuaToken* token =
                tokenAt(index);
            if (expectName &&
                token != nullptr &&
                token->kind ==
                    LuaTokenKind::Identifier)
            {
                names.insert(token->text);
                expectName = false;
            }
        }
        return names;
    }

    StatementFlow parseFor(
        ParserScope& scope)
    {
        const int start = cursor;
        ++cursor;
        if (consumeHeaderThrough(
                QStringLiteral("do")) < 0)
        {
            return parseIncompleteControl(
                scope,
                start,
                QStringLiteral("for"),
                QStringLiteral(
                    "story_graph.parser.missing_do"));
        }
        const QString headerNodeId =
            addControlNode(
                StoryGraphNodeKind::LoopHeader,
                QStringLiteral("for"),
                {start, cursor},
                scope);
        const QSet<QString> savedLocalNames =
            scope.visibleLocalNames;
        const QSet<QString> controlVariableNames =
            forControlVariableNames(
                start,
                cursor);
        scope.visibleLocalNames.unite(
            controlVariableNames);
        BlockFlow body =
            parseBlock(
                scope,
                {QStringLiteral("end")},
                true,
                controlVariableNames.values());
        scope.visibleLocalNames =
            savedLocalNames;
        const int endIndex = cursor;
        consumeExpectedEnd(
            scope,
            headerNodeId,
            QStringLiteral("for"));
        const QString mergeNodeId =
            addControlNode(
                StoryGraphNodeKind::Merge,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "for 出口"),
                anchorSpanAt(endIndex),
                scope,
                StoryGraphCertainty::Certain,
                false);
        if (body.entryNodeId.isEmpty())
        {
            addEdge(
                *controlGraph,
                headerNodeId,
                headerNodeId,
                StoryGraphEdgeKind::LoopBack,
                StoryGraphCertainty::Certain,
                QStringLiteral("empty-for-body"));
        }
        else
        {
            addEdge(
                *controlGraph,
                headerNodeId,
                body.entryNodeId,
                StoryGraphEdgeKind::LoopBody,
                StoryGraphCertainty::Certain,
                QStringLiteral("for-body"));
            for (const QString& exitNodeId :
                 body.exitNodeIds)
            {
                addEdge(
                    *controlGraph,
                    exitNodeId,
                    headerNodeId,
                    StoryGraphEdgeKind::LoopBack,
                    StoryGraphCertainty::Certain,
                    QStringLiteral("for-back"));
            }
        }
        addEdge(
            *controlGraph,
            headerNodeId,
            mergeNodeId,
            StoryGraphEdgeKind::FalseBranch,
            StoryGraphCertainty::Certain,
            QStringLiteral("for-exit"));
        connectBreaksToMerge(
            body.breakNodeIds,
            mergeNodeId);
        return {
            headerNodeId,
            {mergeNodeId},
            {},
            body.returnNodeIds};
    }

    StatementFlow parseRepeat(
        ParserScope& scope)
    {
        const int repeatIndex = cursor;
        ++cursor;
        const QString headerNodeId =
            addControlNode(
                StoryGraphNodeKind::LoopHeader,
                QStringLiteral("repeat"),
                {repeatIndex, cursor},
                scope,
                StoryGraphCertainty::Certain,
                false);
        const QSet<QString> savedLocalNames =
            scope.visibleLocalNames;
        BlockFlow body =
            parseBlock(
                scope,
                {QStringLiteral("until")},
                false);
        const int conditionStart = cursor;
        if (isTextAt(
                cursor,
                QStringLiteral("until")))
        {
            ++cursor;
        }
        else
        {
            addWarning(
                StoryGraphWarningCategory::Syntax,
                StoryGraphWarningSeverity::Warning,
                QStringLiteral(
                    "story_graph.parser.missing_until"),
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "repeat 语句缺少 until"),
                rangeForSpan(
                    anchorSpanAt(cursor)),
                headerNodeId,
                controlGraph);
        }
        const int conditionEnd =
            consumeSimpleStatementEnd(
                cursor,
                {});
        const TokenSpan conditionSpan{
            conditionStart,
            std::max(
                conditionStart + 1,
                conditionEnd)};
        const QString conditionNodeId =
            addControlNode(
                StoryGraphNodeKind::Condition,
                QStringLiteral("until"),
                conditionSpan,
                scope);
        scope.visibleLocalNames =
            savedLocalNames;
        const QString mergeNodeId =
            addControlNode(
                StoryGraphNodeKind::Merge,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "repeat 出口"),
                anchorSpanAt(conditionEnd),
                scope,
                StoryGraphCertainty::Certain,
                false);

        if (body.entryNodeId.isEmpty())
        {
            addEdge(
                *controlGraph,
                headerNodeId,
                conditionNodeId,
                StoryGraphEdgeKind::LoopBody,
                StoryGraphCertainty::Certain,
                QStringLiteral("repeat-empty"));
        }
        else
        {
            addEdge(
                *controlGraph,
                headerNodeId,
                body.entryNodeId,
                StoryGraphEdgeKind::LoopBody,
                StoryGraphCertainty::Certain,
                QStringLiteral("repeat-body"));
            for (const QString& exitNodeId :
                 body.exitNodeIds)
            {
                addEdge(
                    *controlGraph,
                    exitNodeId,
                    conditionNodeId,
                    StoryGraphEdgeKind::Sequential,
                    StoryGraphCertainty::Certain,
                    QStringLiteral(
                        "repeat-condition"));
            }
        }
        addEdge(
            *controlGraph,
            conditionNodeId,
            headerNodeId,
            StoryGraphEdgeKind::LoopBack,
            StoryGraphCertainty::Certain,
            QStringLiteral("until-false"));
        addEdge(
            *controlGraph,
            conditionNodeId,
            mergeNodeId,
            StoryGraphEdgeKind::TrueBranch,
            StoryGraphCertainty::Certain,
            QStringLiteral("until-true"));
        connectBreaksToMerge(
            body.breakNodeIds,
            mergeNodeId);
        return {
            headerNodeId,
            {mergeNodeId},
            {},
            body.returnNodeIds};
    }

    StatementFlow parseDo(
        ParserScope& scope)
    {
        const int start = cursor;
        ++cursor;
        const QString doNodeId =
            addControlNode(
                StoryGraphNodeKind::BasicBlock,
                QStringLiteral("do"),
                {start, cursor},
                scope,
                StoryGraphCertainty::Certain,
                false);
        BlockFlow body =
            parseBlock(
                scope,
                {QStringLiteral("end")},
                true);
        const int endIndex = cursor;
        consumeExpectedEnd(
            scope,
            doNodeId,
            QStringLiteral("do"));
        const QString mergeNodeId =
            addControlNode(
                StoryGraphNodeKind::Merge,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "do 出口"),
                anchorSpanAt(endIndex),
                scope,
                StoryGraphCertainty::Certain,
                false);
        bool fallsThrough =
            body.entryNodeId.isEmpty();
        if (body.entryNodeId.isEmpty())
        {
            addEdge(
                *controlGraph,
                doNodeId,
                mergeNodeId,
                StoryGraphEdgeKind::Sequential,
                StoryGraphCertainty::Certain,
                QStringLiteral("empty-do"));
        }
        else
        {
            addEdge(
                *controlGraph,
                doNodeId,
                body.entryNodeId,
                StoryGraphEdgeKind::Sequential,
                StoryGraphCertainty::Certain,
                QStringLiteral("do-body"));
            for (const QString& exitNodeId :
                 body.exitNodeIds)
            {
                addEdge(
                    *controlGraph,
                    exitNodeId,
                    mergeNodeId,
                    StoryGraphEdgeKind::Fallthrough,
                    StoryGraphCertainty::Certain,
                    QStringLiteral("do-exit"));
                fallsThrough = true;
            }
        }
        StatementFlow result;
        result.entryNodeId = doNodeId;
        if (fallsThrough)
        {
            result.exitNodeIds = {
                mergeNodeId};
        }
        result.breakNodeIds =
            body.breakNodeIds;
        result.returnNodeIds =
            body.returnNodeIds;
        return result;
    }

    StatementFlow parseReturn(
        ParserScope& scope,
        const QSet<QString>& stopWords)
    {
        const int start = cursor;
        ++cursor;
        const int end =
            consumeSimpleStatementEnd(
                cursor,
                stopWords);
        const QString nodeId =
            addControlNode(
                StoryGraphNodeKind::Return,
                QStringLiteral("return"),
                {start, std::max(start + 1, end)},
                scope);
        return {
            nodeId,
            {},
            {},
            {nodeId}};
    }

    StatementFlow parseBreak(
        ParserScope& scope)
    {
        const int start = cursor;
        ++cursor;
        if (isTextAt(
                cursor,
                QStringLiteral(";")))
        {
            ++cursor;
        }
        const QString nodeId =
            addControlNode(
                StoryGraphNodeKind::Break,
                QStringLiteral("break"),
                {start, cursor},
                scope,
                StoryGraphCertainty::Certain,
                false);
        return {
            nodeId,
            {},
            {nodeId},
            {}};
    }

    StatementFlow parseGoto(
        ParserScope& scope)
    {
        const int start = cursor;
        ++cursor;
        QString labelName;
        if (tokenAt(cursor) != nullptr &&
            tokenAt(cursor)->kind ==
                LuaTokenKind::Identifier)
        {
            labelName = textAt(cursor);
            ++cursor;
        }
        const QString nodeId =
            addControlNode(
                StoryGraphNodeKind::Goto,
                QStringLiteral("goto ") +
                    labelName,
                {start, std::max(start + 1, cursor)},
                scope,
                labelName.isEmpty()
                    ? StoryGraphCertainty::Warning
                    : StoryGraphCertainty::Certain,
                false);
        PendingGoto pending;
        pending.nodeId = nodeId;
        pending.labelName = labelName;
        pending.sourceRange =
            rangeForSpan(
                {start, std::max(start + 1, cursor)});
        pending.lexicalBlockId =
            currentLexicalBlockId(scope);
        pending.visibleLocalDeclarationIds =
            scope.visibleLocalDeclarationIds;
        scope.pendingGotos.append(
            std::move(pending));
        return {nodeId, {}, {}, {}};
    }

    StatementFlow parseLabel(
        ParserScope& scope)
    {
        const int start = cursor;
        ++cursor;
        QString labelName;
        if (tokenAt(cursor) != nullptr &&
            tokenAt(cursor)->kind ==
                LuaTokenKind::Identifier)
        {
            labelName = textAt(cursor);
            ++cursor;
        }
        if (isTextAt(
                cursor,
                QStringLiteral("::")))
        {
            ++cursor;
        }
        const QString nodeId =
            addControlNode(
                StoryGraphNodeKind::Label,
                QStringLiteral("label ") +
                    labelName,
                {start, std::max(start + 1, cursor)},
                scope,
                labelName.isEmpty()
                    ? StoryGraphCertainty::Warning
                    : StoryGraphCertainty::Certain,
                false);
        if (!labelName.isEmpty())
        {
            LabelDefinition definition;
            definition.nodeId = nodeId;
            definition.labelName = labelName;
            definition.lexicalBlockId =
                currentLexicalBlockId(scope);
            definition.visibleLocalDeclarationIds =
                scope.visibleLocalDeclarationIds;
            const int lexicalBlockId =
                definition.lexicalBlockId;
            const int labelIndex =
                scope.labels.size();
            scope.labels.append(
                std::move(definition));
            if (lexicalBlockId >= 0 &&
                lexicalBlockId <
                    scope.lexicalBlocks.size())
            {
                scope.lexicalBlocks[
                    lexicalBlockId].
                        terminalLabelIndexes.append(
                            labelIndex);
            }

            const QString labelKey =
                scopedLabelKey(
                    lexicalBlockId,
                    labelName);
            if (scope.labelIndexByBlockAndName.
                    contains(labelKey))
            {
                scope.duplicateLabelKeys.insert(
                    labelKey);
                for (StoryGraphNode& node :
                     controlGraph->nodes)
                {
                    if (node.id == nodeId)
                    {
                        node.certainty =
                            StoryGraphCertainty::Warning;
                        break;
                    }
                }
                addWarning(
                    StoryGraphWarningCategory::ControlFlow,
                    StoryGraphWarningSeverity::Warning,
                    QStringLiteral(
                        "story_graph.control.duplicate_label"),
                    QCoreApplication::translate(
                        "StoryGraphAnalyzer",
                        "同一 Lua 块内存在重复 label"),
                    rangeForSpan(
                        {start, cursor}),
                    nodeId,
                    controlGraph);
            }
            else
            {
                scope.labelIndexByBlockAndName.insert(
                    labelKey,
                    labelIndex);
            }
        }
        return {
            nodeId,
            {nodeId},
            {},
            {}};
    }

    StatementFlow parseFunctionDefinition(
        ParserScope& outerScope)
    {
        const int start = cursor;
        bool localFunction = false;
        bool globalDeclaration = false;
        if (isTextAt(
                cursor,
                QStringLiteral("local")))
        {
            localFunction = true;
            ++cursor;
        }
        else if (isTextAt(
                     cursor,
                     QStringLiteral("global")))
        {
            globalDeclaration = true;
            ++cursor;
        }
        if (!isTextAt(
                cursor,
                QStringLiteral("function")))
        {
            cursor = start;
            return parseSimpleStatement(
                outerScope,
                {});
        }
        ++cursor;

        QString functionName;
        while (!atEnd() &&
               !isTextAt(
                   cursor,
                   QStringLiteral("(")))
        {
            functionName.append(
                textAt(cursor));
            ++cursor;
        }
        if (functionName.isEmpty())
        {
            functionName =
                QStringLiteral("<anonymous>");
        }

        QList<QString> parameters;
        if (isTextAt(
                cursor,
                QStringLiteral("(")))
        {
            int depth = 0;
            do
            {
                const QString text =
                    textAt(cursor);
                if (text == QStringLiteral("("))
                    ++depth;
                else if (text == QStringLiteral(")"))
                    --depth;
                else if (depth == 1 &&
                         tokenAt(cursor) != nullptr &&
                         tokenAt(cursor)->kind ==
                             LuaTokenKind::Identifier)
                {
                    parameters.append(text);
                }
                ++cursor;
            }
            while (!atEnd() && depth > 0);
        }
        const int headerEnd = cursor;
        const QString definitionNodeId =
            addControlNode(
                StoryGraphNodeKind::Statement,
                QStringLiteral("function ") +
                    functionName,
                {start, std::max(start + 1, headerEnd)},
                outerScope,
                StoryGraphCertainty::Certain,
                false);

        const QString simpleFunctionName =
            functionName.contains(
                QLatin1Char('.')) ||
                functionName.contains(
                    QLatin1Char(':'))
            ? QString()
            : functionName;
        const bool usesLocalEnvironment =
            !globalDeclaration &&
            outerScope.visibleLocalNames.contains(
                QStringLiteral("_ENV"));
        const bool assignsVisibleLocal =
            !globalDeclaration &&
            (usesLocalEnvironment ||
             outerScope.visibleLocalNames.contains(
                 simpleFunctionName));
        if (localFunction &&
            !simpleFunctionName.isEmpty())
        {
            outerScope.visibleLocalNames.insert(
                simpleFunctionName);
            registerLocalDeclaration(
                outerScope);
        }
        if (!localFunction &&
            !assignsVisibleLocal &&
            !simpleFunctionName.isEmpty() &&
            StoryGraphRuntimeApiCatalog::
                containsExact(
                    simpleFunctionName))
        {
            outerScope.mutatedGlobalNames.insert(
                simpleFunctionName);
        }
        QString environmentName;
        QString environmentTableName;
        for (const QString& candidateTable :
             {QStringLiteral("_ENV"),
              QStringLiteral("_G")})
        {
            const QString dotPrefix =
                candidateTable +
                QLatin1Char('.');
            const QString methodPrefix =
                candidateTable +
                QLatin1Char(':');
            if (functionName.startsWith(
                    dotPrefix))
            {
                environmentTableName =
                    candidateTable;
                environmentName =
                    functionName.mid(
                        dotPrefix.size());
                break;
            }
            if (functionName.startsWith(
                    methodPrefix))
            {
                environmentTableName =
                    candidateTable;
                environmentName =
                    functionName.mid(
                        methodPrefix.size());
                break;
            }
        }
        if (!localFunction &&
            !environmentTableName.isEmpty() &&
            !outerScope.visibleLocalNames.contains(
                environmentTableName) &&
            !environmentName.isEmpty() &&
            !environmentName.contains(
                QLatin1Char('.')) &&
            !environmentName.contains(
                QLatin1Char(':')))
        {
            if (StoryGraphRuntimeApiCatalog::
                    containsExact(
                        environmentName))
            {
                outerScope.mutatedGlobalNames.insert(
                    environmentName);
            }
        }

        const QString functionKey =
            outerScope.id +
            QStringLiteral("|") +
            functionName;
        const int functionOccurrence =
            functionOccurrenceCounts.value(
                functionKey);
        functionOccurrenceCounts.insert(
            functionKey,
            functionOccurrence + 1);

        ParserScope functionScope;
        functionScope.id =
            outerScope.id +
            QStringLiteral("/function:") +
            functionName +
            QLatin1Char('#') +
            QString::number(
                functionOccurrence);
        functionScope.visibleLocalNames =
            outerScope.visibleLocalNames;
        functionScope.mutatedGlobalNames =
            outerScope.mutatedGlobalNames;
        for (const QString& parameter :
             parameters)
        {
            functionScope.visibleLocalNames.insert(
                parameter);
        }

        const QString functionEntryNodeId =
            addNode(
                *controlGraph,
                StoryGraphNodeKind::FunctionEntry,
                StoryGraphCertainty::Certain,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "函数入口"),
                functionName,
                functionScope.id,
                QStringLiteral("function-entry:") +
                    functionName,
                {start, std::max(start + 1, headerEnd)});
        BlockFlow body =
            parseBlock(
                functionScope,
                {QStringLiteral("end")},
                false,
                parameters);
        const int endIndex = cursor;
        if (isTextAt(
                cursor,
                QStringLiteral("end")))
        {
            ++cursor;
        }
        else
        {
            addWarning(
                StoryGraphWarningCategory::Syntax,
                StoryGraphWarningSeverity::Warning,
                QStringLiteral(
                    "story_graph.parser.missing_function_end"),
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "function 定义缺少 end"),
                rangeForSpan(
                    anchorSpanAt(endIndex)),
                functionEntryNodeId,
                controlGraph);
        }
        const QString functionExitNodeId =
            addNode(
                *controlGraph,
                StoryGraphNodeKind::FunctionExit,
                StoryGraphCertainty::Certain,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "函数出口"),
                functionName,
                functionScope.id,
                QStringLiteral("function-exit:") +
                    functionName,
                anchorSpanAt(endIndex));
        if (body.entryNodeId.isEmpty())
        {
            addEdge(
                *controlGraph,
                functionEntryNodeId,
                functionExitNodeId,
                StoryGraphEdgeKind::Sequential,
                StoryGraphCertainty::Certain,
                QStringLiteral("empty-function"));
        }
        else
        {
            addEdge(
                *controlGraph,
                functionEntryNodeId,
                body.entryNodeId,
                StoryGraphEdgeKind::Sequential,
                StoryGraphCertainty::Certain,
                QStringLiteral("function-body"));
            for (const QString& exitNodeId :
                 body.exitNodeIds)
            {
                addEdge(
                    *controlGraph,
                    exitNodeId,
                    functionExitNodeId,
                    StoryGraphEdgeKind::Fallthrough,
                    StoryGraphCertainty::Certain,
                    QStringLiteral("function-exit"));
            }
        }
        for (const QString& returnNodeId :
             body.returnNodeIds)
        {
            addEdge(
                *controlGraph,
                returnNodeId,
                functionExitNodeId,
                StoryGraphEdgeKind::Return,
                StoryGraphCertainty::Certain,
                QStringLiteral("function-return"));
        }
        for (const QString& breakNodeId :
             body.breakNodeIds)
        {
            addWarning(
                StoryGraphWarningCategory::ControlFlow,
                StoryGraphWarningSeverity::Warning,
                QStringLiteral(
                    "story_graph.control.break_outside_loop"),
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "function 中的 break 不在可解析循环内"),
                rangeForNode(
                    *controlGraph,
                    breakNodeId),
                breakNodeId,
                controlGraph);
        }
        resolveGotos(
            functionScope,
            functionExitNodeId);

        if (globalDeclaration &&
            !simpleFunctionName.isEmpty())
        {
            outerScope.mutatedGlobalNames.insert(
                simpleFunctionName);
        }
        return {
            definitionNodeId,
            {definitionNodeId},
            {},
            {}};
    }

    int topLevelAssignmentIndex(
        const TokenSpan& span)
    {
        int parenthesisDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;
        for (int index = span.first;
             index < span.lastExclusive;
             ++index)
        {
            if (checkCancellation())
                return -1;
            const QString text = textAt(index);
            if (parenthesisDepth == 0 &&
                bracketDepth == 0 &&
                braceDepth == 0 &&
                text == QStringLiteral("="))
            {
                return index;
            }
            if (text == QStringLiteral("("))
                ++parenthesisDepth;
            else if (text == QStringLiteral(")"))
                parenthesisDepth =
                    std::max(0, parenthesisDepth - 1);
            else if (text == QStringLiteral("["))
                ++bracketDepth;
            else if (text == QStringLiteral("]"))
                bracketDepth =
                    std::max(0, bracketDepth - 1);
            else if (text == QStringLiteral("{"))
                ++braceDepth;
            else if (text == QStringLiteral("}"))
                braceDepth =
                    std::max(0, braceDepth - 1);
        }
        return -1;
    }

    bool isGlobalDeclarationStart(
        int index,
        int limit) const
    {
        if (!isTextAt(
                index,
                QStringLiteral("global")) ||
            index + 1 >= limit)
        {
            return false;
        }
        const LuaToken* lookahead =
            tokenAt(index + 1);
        return isTextAt(
                   index + 1,
                   QStringLiteral("<")) ||
            isTextAt(
                   index + 1,
                   QStringLiteral("*")) ||
            isTextAt(
                   index + 1,
                   QStringLiteral("function")) ||
            (lookahead != nullptr &&
             lookahead->kind ==
                 LuaTokenKind::Identifier);
    }

    bool consumeDeclarationAttribute(
        int& index,
        int limit) const
    {
        if (!isTextAt(
                index,
                QStringLiteral("<")))
        {
            return true;
        }
        const LuaToken* attributeName =
            tokenAt(index + 1);
        if (index + 2 >= limit ||
            attributeName == nullptr ||
            attributeName->kind !=
                LuaTokenKind::Identifier ||
            !isTextAt(
                index + 2,
                QStringLiteral(">")))
        {
            return false;
        }
        index += 3;
        return true;
    }

    DeclarationTargets parseDeclarationTargets(
        const TokenSpan& span,
        int targetLimit) const
    {
        DeclarationTargets result;
        if (!span.isValid() ||
            targetLimit <= span.first)
        {
            return result;
        }

        int index = span.first;
        if (isTextAt(
                index,
                QStringLiteral("local")))
        {
            result.declaration = true;
            ++index;
        }
        else if (isGlobalDeclarationStart(
                     index,
                     targetLimit))
        {
            result.declaration = true;
            ++index;
        }
        else
        {
            return result;
        }

        if (!consumeDeclarationAttribute(
                index,
                targetLimit))
        {
            return result;
        }
        if (isTextAt(
                index,
                QStringLiteral("*")))
        {
            result.complete =
                index + 1 == targetLimit;
            return result;
        }

        while (index < targetLimit)
        {
            const LuaToken* nameToken =
                tokenAt(index);
            if (nameToken == nullptr ||
                nameToken->kind !=
                    LuaTokenKind::Identifier)
            {
                return result;
            }
            result.names.append(
                {index, index + 1});
            ++index;
            if (!consumeDeclarationAttribute(
                    index,
                    targetLimit))
            {
                return result;
            }
            if (index == targetLimit)
            {
                result.complete = true;
                return result;
            }
            if (!isTextAt(
                    index,
                    QStringLiteral(",")))
            {
                return result;
            }
            ++index;
        }
        return result;
    }

    QString addOrdinaryVariableNode(
        StoryGraphNodeKind kind,
        StoryGraphCertainty certainty,
        const QString& variableName,
        const TokenSpan& span,
        ParserScope& scope,
        const QString& controlNodeId,
        const QString& fingerprintPrefix)
    {
        const QString nodeId =
            addNode(
                *semanticGraph,
                kind,
                certainty,
                kind ==
                        StoryGraphNodeKind::VariableWrite
                    ? QCoreApplication::translate(
                          "StoryGraphAnalyzer",
                          "变量写入")
                    : QCoreApplication::translate(
                          "StoryGraphAnalyzer",
                          "变量读取"),
                variableName,
                scope.id,
                fingerprintPrefix +
                    QLatin1Char(':') +
                    variableName +
                    QLatin1Char(':') +
                    fingerprintForSpan(span),
                span);
        StoryGraphNode& node =
            semanticGraph->nodes.last();
        node.controlFlowNodeId =
            controlNodeId;
        node.variableName = variableName;
        appendSemanticNodeForControl(
            controlNodeId,
            nodeId);
        return nodeId;
    }

    void extractAssignmentSemantics(
        const TokenSpan& span,
        int assignmentIndex,
        ParserScope& scope,
        const QString& controlNodeId)
    {
        int semanticEnd =
            span.lastExclusive;
        if (isTextAt(
                semanticEnd - 1,
                QStringLiteral(";")))
        {
            --semanticEnd;
        }
        const TokenSpan rightHandSide{
            assignmentIndex + 1,
            semanticEnd};
        if (rightHandSide.isValid())
        {
            extractCallsInRange(
                rightHandSide.first,
                rightHandSide.lastExclusive,
                scope,
                controlNodeId,
                true);
            if (cancelled)
                return;
        }

        QList<TokenSpan> simpleTargets;
        bool complexTargetPresent = false;
        const DeclarationTargets declaration =
            parseDeclarationTargets(
                span,
                assignmentIndex);
        if (declaration.declaration)
        {
            simpleTargets =
                declaration.names;
            complexTargetPresent =
                !declaration.complete;
        }
        else
        {
            int targetStart = span.first;
            int parenthesisDepth = 0;
            int bracketDepth = 0;
            int braceDepth = 0;
            for (int index = span.first;
                 index <= assignmentIndex;
                 ++index)
            {
                if (checkCancellation())
                    return;
                const QString text = textAt(index);
                if (text == QStringLiteral("("))
                    ++parenthesisDepth;
                else if (text == QStringLiteral(")"))
                    parenthesisDepth =
                        std::max(
                            0,
                            parenthesisDepth - 1);
                else if (text == QStringLiteral("["))
                    ++bracketDepth;
                else if (text == QStringLiteral("]"))
                    bracketDepth =
                        std::max(
                            0,
                            bracketDepth - 1);
                else if (text == QStringLiteral("{"))
                    ++braceDepth;
                else if (text == QStringLiteral("}"))
                    braceDepth =
                        std::max(
                            0,
                            braceDepth - 1);

                const bool separator =
                    index == assignmentIndex ||
                    (parenthesisDepth == 0 &&
                     bracketDepth == 0 &&
                     braceDepth == 0 &&
                     text == QStringLiteral(","));
                if (!separator)
                    continue;

                const TokenSpan target{
                    targetStart,
                    index};
                const LuaToken* targetToken =
                    tokenAt(targetStart);
                if (target.lastExclusive ==
                        target.first + 1 &&
                    targetToken != nullptr &&
                    targetToken->kind ==
                        LuaTokenKind::Identifier)
                {
                    simpleTargets.append(
                        target);
                }
                else if (target.isValid())
                {
                    complexTargetPresent = true;
                }
                targetStart = index + 1;
            }
        }

        if (complexTargetPresent)
        {
            addWarning(
                StoryGraphWarningCategory::Semantic,
                StoryGraphWarningSeverity::Information,
                QStringLiteral(
                    "story_graph.semantic.complex_assignment_target"),
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "复杂 Lua 赋值目标未展开为确定变量写入"),
                rangeForSpan(
                    {span.first, assignmentIndex}),
                controlNodeId,
                semanticGraph);
        }

        const StoryGraphCertainty writeCertainty =
            rightHandSide.isValid()
            ? StoryGraphCertainty::Certain
            : StoryGraphCertainty::Warning;
        for (const TokenSpan& target :
             simpleTargets)
        {
            if (checkCancellation())
                return;
            addOrdinaryVariableNode(
                StoryGraphNodeKind::VariableWrite,
                writeCertainty,
                textAt(target.first),
                target,
                scope,
                controlNodeId,
                QStringLiteral(
                    "lua-variable-write"));
        }
    }

    StatementFlow parseSimpleStatement(
        ParserScope& scope,
        const QSet<QString>& stopWords)
    {
        const int start = cursor;
        const int end =
            consumeSimpleStatementEnd(
                cursor,
                stopWords);
        const TokenSpan span{
            start,
            std::max(start + 1, end)};
        const int assignmentIndex =
            topLevelAssignmentIndex(span);
        const QString nodeId =
            addControlNode(
                StoryGraphNodeKind::Statement,
                QStringLiteral("statement"),
                span,
                scope,
                StoryGraphCertainty::Certain,
                assignmentIndex < 0);
        if (assignmentIndex >= 0)
        {
            extractAssignmentSemantics(
                span,
                assignmentIndex,
                scope,
                nodeId);
        }
        updateScopeAfterStatement(
            span,
            scope);
        return {
            nodeId,
            {nodeId},
            {},
            {}};
    }

    StatementFlow parseIncompleteControl(
        ParserScope& scope,
        int start,
        const QString& title,
        const QString& diagnosticCode)
    {
        const TokenSpan span{
            start,
            std::max(start + 1, cursor)};
        const QString nodeId =
            addControlNode(
                StoryGraphNodeKind::Warning,
                title,
                span,
                scope,
                StoryGraphCertainty::Warning);
        addWarning(
            StoryGraphWarningCategory::Syntax,
            StoryGraphWarningSeverity::Warning,
            diagnosticCode,
            QCoreApplication::translate(
                "StoryGraphAnalyzer",
                "控制流语句头部不完整"),
            rangeForSpan(span),
            nodeId,
            controlGraph);
        return {
            nodeId,
            {nodeId},
            {},
            {}};
    }

    void consumeExpectedEnd(
        ParserScope& scope,
        const QString& relatedNodeId,
        const QString& statementName)
    {
        if (isTextAt(
                cursor,
                QStringLiteral("end")))
        {
            ++cursor;
            return;
        }
        addWarning(
            StoryGraphWarningCategory::Syntax,
            StoryGraphWarningSeverity::Warning,
            QStringLiteral(
                "story_graph.parser.missing_end"),
            QCoreApplication::translate(
                "StoryGraphAnalyzer",
                "控制流语句缺少 end") +
                QStringLiteral(": ") +
                statementName,
            rangeForSpan(
                anchorSpanAt(cursor)),
            relatedNodeId,
            controlGraph);
        Q_UNUSED(scope);
    }

    void connectBreaksToMerge(
        const QList<QString>& breakNodeIds,
        const QString& mergeNodeId)
    {
        for (const QString& breakNodeId :
             breakNodeIds)
        {
            addEdge(
                *controlGraph,
                breakNodeId,
                mergeNodeId,
                StoryGraphEdgeKind::Break,
                StoryGraphCertainty::Certain,
                QStringLiteral("loop-break"));
        }
    }

    void resolveGotos(
        ParserScope& scope,
        const QString& fallbackExitNodeId)
    {
        for (const PendingGoto& pending :
             scope.pendingGotos)
        {
            if (checkCancellation())
                return;

            int labelIndex = -1;
            bool duplicateTarget = false;
            int lexicalBlockId =
                pending.lexicalBlockId;
            while (lexicalBlockId >= 0 &&
                   lexicalBlockId <
                       scope.lexicalBlocks.size())
            {
                if (checkCancellation())
                    return;
                const QString labelKey =
                    scopedLabelKey(
                        lexicalBlockId,
                        pending.labelName);
                if (scope.labelIndexByBlockAndName.
                        contains(labelKey))
                {
                    labelIndex =
                        scope.labelIndexByBlockAndName.
                            value(
                                labelKey,
                                -1);
                    duplicateTarget =
                        scope.duplicateLabelKeys.contains(
                            labelKey);
                    break;
                }
                lexicalBlockId =
                    scope.lexicalBlocks.at(
                        lexicalBlockId).
                            parentBlockId;
            }

            bool entersLocalScope = false;
            if (labelIndex >= 0 &&
                labelIndex < scope.labels.size())
            {
                const LabelDefinition& label =
                    scope.labels.at(labelIndex);
                for (int declarationId :
                     label.visibleLocalDeclarationIds)
                {
                    if (checkCancellation())
                        return;
                    if (!pending.
                            visibleLocalDeclarationIds.
                                contains(
                                    declarationId))
                    {
                        entersLocalScope = true;
                        break;
                    }
                }
            }

            if (labelIndex >= 0 &&
                labelIndex < scope.labels.size() &&
                !duplicateTarget &&
                !entersLocalScope)
            {
                const QString targetNodeId =
                    scope.labels.at(
                        labelIndex).nodeId;
                addEdge(
                    *controlGraph,
                    pending.nodeId,
                    targetNodeId,
                    StoryGraphEdgeKind::Goto,
                    StoryGraphCertainty::Certain,
                    QStringLiteral("goto:") +
                        pending.labelName);
                continue;
            }

            for (StoryGraphNode& node :
                 controlGraph->nodes)
            {
                if (node.id == pending.nodeId)
                {
                    node.certainty =
                        StoryGraphCertainty::Warning;
                    break;
                }
            }

            QString diagnosticCode;
            QString message;
            QString edgeFingerprint;
            if (duplicateTarget)
            {
                diagnosticCode =
                    QStringLiteral(
                        "story_graph.control.ambiguous_goto");
                message =
                    QCoreApplication::translate(
                        "StoryGraphAnalyzer",
                        "goto 目标 label 在同一块内重复");
                edgeFingerprint =
                    QStringLiteral(
                        "ambiguous-goto");
            }
            else if (entersLocalScope)
            {
                diagnosticCode =
                    QStringLiteral(
                        "story_graph.control.goto_into_local_scope");
                message =
                    QCoreApplication::translate(
                        "StoryGraphAnalyzer",
                        "goto 不得跳入尚不可见的 local 变量作用域");
                edgeFingerprint =
                    QStringLiteral(
                        "goto-into-local-scope");
            }
            else
            {
                diagnosticCode =
                    QStringLiteral(
                        "story_graph.control.unresolved_goto");
                message =
                    QCoreApplication::translate(
                        "StoryGraphAnalyzer",
                        "goto 目标 label 无法解析");
                edgeFingerprint =
                    QStringLiteral(
                        "unresolved-goto");
            }
            addWarning(
                StoryGraphWarningCategory::ControlFlow,
                StoryGraphWarningSeverity::Warning,
                diagnosticCode,
                message,
                pending.sourceRange,
                pending.nodeId,
                controlGraph);
            addEdge(
                *controlGraph,
                pending.nodeId,
                fallbackExitNodeId,
                StoryGraphEdgeKind::Unknown,
                StoryGraphCertainty::Warning,
                edgeFingerprint);
        }
    }

    bool canStartStatement(
        int index) const
    {
        const LuaToken* token =
            tokenAt(index);
        if (token == nullptr)
            return false;
        if (token->text ==
                QStringLiteral("::"))
        {
            return true;
        }
        if (token->kind ==
            LuaTokenKind::Keyword)
        {
            static const QSet<QString> starters = {
                QStringLiteral("break"),
                QStringLiteral("do"),
                QStringLiteral("for"),
                QStringLiteral("function"),
                QStringLiteral("goto"),
                QStringLiteral("if"),
                QStringLiteral("local"),
                QStringLiteral("repeat"),
                QStringLiteral("return"),
                QStringLiteral("while")
            };
            return starters.contains(
                token->text);
        }
        if (token->kind !=
            LuaTokenKind::Identifier)
        {
            return false;
        }
        return true;
    }

    bool canEndExpression(
        int index) const
    {
        const LuaToken* token =
            tokenAt(index);
        if (token == nullptr)
            return false;
        if (token->kind ==
                LuaTokenKind::Identifier ||
            token->kind ==
                LuaTokenKind::Number ||
            token->kind ==
                LuaTokenKind::String ||
            token->kind ==
                LuaTokenKind::LongString)
        {
            return true;
        }
        if (token->kind ==
                LuaTokenKind::Keyword &&
            (token->text ==
                 QStringLiteral("true") ||
             token->text ==
                 QStringLiteral("false") ||
             token->text ==
                 QStringLiteral("nil") ||
             token->text ==
                 QStringLiteral("end")))
        {
            return true;
        }
        return token->text ==
                QStringLiteral(")") ||
            token->text ==
                QStringLiteral("]") ||
            token->text ==
                QStringLiteral("}");
    }

    int consumeSimpleStatementEnd(
        int start,
        const QSet<QString>& stopWords)
    {
        cursor = start;
        int parenthesisDepth = 0;
        int bracketDepth = 0;
        int braceDepth = 0;
        QList<QString> nestedBlockClosers;
        QList<bool> nestedBlockAwaitingDo;
        while (!atEnd())
        {
            if (checkCancellation())
                break;
            const QString text =
                textAt(cursor);
            const bool balanced =
                parenthesisDepth == 0 &&
                bracketDepth == 0 &&
                braceDepth == 0 &&
                nestedBlockClosers.isEmpty();
            if (balanced &&
                cursor > start)
            {
                if (text ==
                    QStringLiteral(";"))
                {
                    ++cursor;
                    break;
                }
                if (stopWords.contains(text) ||
                    text ==
                        QStringLiteral("::"))
                {
                    break;
                }
                const LuaToken& currentToken =
                    tokens.at(cursor);
                const LuaToken& previousToken =
                    tokens.at(cursor - 1);
                if (canStartStatement(cursor) &&
                    canEndExpression(cursor - 1) &&
                    (currentToken.range.start.line >
                         previousToken.range.start.line ||
                     previousToken.text ==
                         QStringLiteral(")")))
                {
                    break;
                }
            }

            if (!nestedBlockClosers.isEmpty())
            {
                if (text ==
                    nestedBlockClosers.constLast())
                {
                    nestedBlockClosers.removeLast();
                    nestedBlockAwaitingDo.removeLast();
                }
                else if (text ==
                         QStringLiteral("function") ||
                         text ==
                         QStringLiteral("if"))
                {
                    nestedBlockClosers.append(
                        QStringLiteral("end"));
                    nestedBlockAwaitingDo.append(false);
                }
                else if (text ==
                         QStringLiteral("while") ||
                         text ==
                         QStringLiteral("for"))
                {
                    nestedBlockClosers.append(
                        QStringLiteral("end"));
                    nestedBlockAwaitingDo.append(true);
                }
                else if (text ==
                         QStringLiteral("repeat"))
                {
                    nestedBlockClosers.append(
                        QStringLiteral("until"));
                    nestedBlockAwaitingDo.append(false);
                }
                else if (text ==
                         QStringLiteral("do"))
                {
                    if (!nestedBlockAwaitingDo.isEmpty() &&
                        nestedBlockAwaitingDo.constLast())
                    {
                        nestedBlockAwaitingDo.last() = false;
                    }
                    else
                    {
                        nestedBlockClosers.append(
                            QStringLiteral("end"));
                        nestedBlockAwaitingDo.append(false);
                    }
                }
            }
            else if (cursor > start &&
                     text ==
                         QStringLiteral("function"))
            {
                nestedBlockClosers.append(
                    QStringLiteral("end"));
                nestedBlockAwaitingDo.append(false);
            }

            if (text == QStringLiteral("("))
                ++parenthesisDepth;
            else if (text == QStringLiteral(")"))
                parenthesisDepth =
                    std::max(0, parenthesisDepth - 1);
            else if (text == QStringLiteral("["))
                ++bracketDepth;
            else if (text == QStringLiteral("]"))
                bracketDepth =
                    std::max(0, bracketDepth - 1);
            else if (text == QStringLiteral("{"))
                ++braceDepth;
            else if (text == QStringLiteral("}"))
                braceDepth =
                    std::max(0, braceDepth - 1);
            ++cursor;
        }
        return cursor;
    }

    void updateScopeAfterStatement(
        const TokenSpan& span,
        ParserScope& scope)
    {
        if (!span.isValid())
            return;
        int semanticEnd =
            span.lastExclusive;
        if (isTextAt(
                semanticEnd - 1,
                QStringLiteral(";")))
        {
            --semanticEnd;
        }
        const TokenSpan semanticSpan{
            span.first,
            semanticEnd};
        const int assignmentIndex =
            topLevelAssignmentIndex(
                semanticSpan);
        if (cancelled)
            return;

        const int declarationEnd =
            assignmentIndex >= 0
            ? assignmentIndex
            : semanticEnd;
        const DeclarationTargets declaration =
            parseDeclarationTargets(
                semanticSpan,
                declarationEnd);
        if (declaration.declaration)
        {
            if (isTextAt(
                    span.first,
                    QStringLiteral("local")))
            {
                for (const TokenSpan& nameSpan :
                     declaration.names)
                {
                    if (checkCancellation())
                        return;
                    scope.visibleLocalNames.insert(
                        textAt(nameSpan.first));
                    registerLocalDeclaration(
                        scope);
                }
            }
            else if (assignmentIndex >= 0)
            {
                for (const TokenSpan& nameSpan :
                     declaration.names)
                {
                    if (checkCancellation())
                        return;
                    const QString name =
                        textAt(nameSpan.first);
                    if (StoryGraphRuntimeApiCatalog::
                            containsExact(name))
                    {
                        scope.mutatedGlobalNames.insert(
                            name);
                    }
                }
            }
            return;
        }

        if (assignmentIndex < 0)
            return;

        int lvalueStart = span.first;
        int bracketDepth = 0;
        for (int index = span.first;
             index <= assignmentIndex;
             ++index)
        {
            if (checkCancellation())
                return;
            const QString text = textAt(index);
            if (text == QStringLiteral("["))
                ++bracketDepth;
            else if (text == QStringLiteral("]"))
                bracketDepth =
                    std::max(0, bracketDepth - 1);
            const bool separator =
                index == assignmentIndex ||
                (bracketDepth == 0 &&
                 text == QStringLiteral(","));
            if (!separator)
                continue;
            markRuntimeApiLvalue(
                {lvalueStart, index},
                scope);
            lvalueStart = index + 1;
        }
    }

    void markRuntimeApiLvalue(
        const TokenSpan& span,
        ParserScope& scope)
    {
        if (!span.isValid())
            return;
        const int tokenLength =
            span.lastExclusive - span.first;
        const LuaToken* firstToken =
            tokenAt(span.first);
        if (tokenLength == 1 &&
            firstToken != nullptr &&
            firstToken->kind ==
                LuaTokenKind::Identifier)
        {
            if (scope.visibleLocalNames.contains(
                    firstToken->text) ||
                scope.visibleLocalNames.contains(
                    QStringLiteral("_ENV")))
            {
                return;
            }
            if (firstToken->text ==
                    QStringLiteral("_ENV") ||
                StoryGraphRuntimeApiCatalog::
                    containsExact(
                        firstToken->text))
            {
                scope.mutatedGlobalNames.insert(
                    firstToken->text);
            }
            return;
        }
        if (firstToken == nullptr)
        {
            return;
        }
        const QString environmentTableName =
            firstToken->text;
        const bool directEnvironmentTable =
            environmentTableName ==
                QStringLiteral("_ENV");
        const bool globalLibraryTable =
            environmentTableName ==
                QStringLiteral("_G");
        if ((!directEnvironmentTable &&
             !globalLibraryTable) ||
            scope.visibleLocalNames.contains(
                environmentTableName))
        {
            return;
        }

        QString runtimeName;
        if (tokenLength == 3 &&
            isTextAt(
                span.first + 1,
                QStringLiteral(".")))
        {
            const LuaToken* nameToken =
                tokenAt(span.first + 2);
            if (nameToken != nullptr &&
                nameToken->kind ==
                    LuaTokenKind::Identifier)
            {
                runtimeName = nameToken->text;
            }
        }
        else if (isTextAt(
                     span.first + 1,
                     QStringLiteral("[")))
        {
            const int closingIndex =
                matchingClosingToken(
                    span.first + 1,
                    span.lastExclusive,
                    QStringLiteral("["),
                    QStringLiteral("]"));
            if (closingIndex !=
                span.lastExclusive - 1)
            {
                return;
            }
            const TokenSpan keySpan{
                span.first + 2,
                closingIndex};
            if (isSingleStringLiteral(
                    keySpan,
                    &runtimeName))
            {
                if (StoryGraphRuntimeApiCatalog::
                        containsExact(
                            runtimeName))
                {
                    scope.mutatedGlobalNames.insert(
                        runtimeName);
                }
                return;
            }
            const LuaToken* keyToken =
                keySpan.lastExclusive ==
                        keySpan.first + 1
                ? tokenAt(keySpan.first)
                : nullptr;
            if (keyToken != nullptr &&
                (keyToken->kind ==
                     LuaTokenKind::Number ||
                 (keyToken->kind ==
                      LuaTokenKind::Keyword &&
                  (keyToken->text ==
                       QStringLiteral("true") ||
                   keyToken->text ==
                       QStringLiteral("false") ||
                   keyToken->text ==
                       QStringLiteral("nil")))))
            {
                return;
            }
            scope.mutatedGlobalNames.insert(
                QStringLiteral("_ENV"));
            return;
        }
        if (StoryGraphRuntimeApiCatalog::
                containsExact(runtimeName))
        {
            scope.mutatedGlobalNames.insert(
                runtimeName);
        }
    }

    bool isCallSuffixStart(
        int index) const
    {
        const LuaToken* token =
            tokenAt(index);
        return isTextAt(
                   index,
                   QStringLiteral("(")) ||
            isTextAt(
                   index,
                   QStringLiteral("{")) ||
            (token != nullptr &&
             (token->kind ==
                  LuaTokenKind::String ||
              token->kind ==
                  LuaTokenKind::LongString));
    }

    bool isPostfixSuffixStart(
        int index) const
    {
        return isCallSuffixStart(index) ||
            isTextAt(
                index,
                QStringLiteral("[")) ||
            ((isTextAt(
                  index,
                  QStringLiteral(".")) ||
              isTextAt(
                  index,
                  QStringLiteral(":"))) &&
             tokenAt(index + 1) != nullptr &&
             tokenAt(index + 1)->kind ==
                 LuaTokenKind::Identifier);
    }

    CallArguments parseCallArguments(
        int openingTokenIndex,
        int limit)
    {
        CallArguments result;
        const LuaToken* openingToken =
            tokenAt(openingTokenIndex);
        if (openingToken != nullptr &&
            (openingToken->kind ==
                 LuaTokenKind::String ||
             openingToken->kind ==
                 LuaTokenKind::LongString))
        {
            result.arguments.append(
                {openingTokenIndex,
                 openingTokenIndex + 1});
            result.closingTokenIndex =
                openingTokenIndex;
            result.complete =
                openingToken->complete;
            return result;
        }
        if (isTextAt(
                openingTokenIndex,
                QStringLiteral("{")))
        {
            const int closingIndex =
                matchingClosingToken(
                    openingTokenIndex,
                    limit,
                    QStringLiteral("{"),
                    QStringLiteral("}"));
            result.arguments.append(
                {openingTokenIndex,
                 closingIndex >= 0
                    ? closingIndex + 1
                    : limit});
            result.closingTokenIndex =
                closingIndex;
            result.complete =
                closingIndex >= 0;
            return result;
        }
        if (!isTextAt(
                openingTokenIndex,
                QStringLiteral("(")))
        {
            return result;
        }
        int parenthesisDepth = 1;
        int bracketDepth = 0;
        int braceDepth = 0;
        int argumentStart =
            openingTokenIndex + 1;
        for (int index =
                 openingTokenIndex + 1;
             index < limit;
             ++index)
        {
            if (checkCancellation())
                return result;
            const QString text =
                textAt(index);
            if (text == QStringLiteral("function"))
            {
                const int functionEnd =
                    skipFunctionExpression(
                        index,
                        limit);
                if (cancelled)
                    return result;
                index = std::max(
                    index,
                    functionEnd - 1);
                continue;
            }
            if (text == QStringLiteral("("))
                ++parenthesisDepth;
            else if (text == QStringLiteral(")"))
            {
                --parenthesisDepth;
                if (parenthesisDepth == 0)
                {
                    if (index > argumentStart)
                    {
                        result.arguments.append(
                            {argumentStart, index});
                    }
                    result.closingTokenIndex =
                        index;
                    result.complete = true;
                    return result;
                }
            }
            else if (text == QStringLiteral("["))
                ++bracketDepth;
            else if (text == QStringLiteral("]"))
                bracketDepth =
                    std::max(0, bracketDepth - 1);
            else if (text == QStringLiteral("{"))
                ++braceDepth;
            else if (text == QStringLiteral("}"))
                braceDepth =
                    std::max(0, braceDepth - 1);
            else if (text == QStringLiteral(",") &&
                     parenthesisDepth == 1 &&
                     bracketDepth == 0 &&
                     braceDepth == 0)
            {
                result.arguments.append(
                    {argumentStart, index});
                argumentStart = index + 1;
            }
        }
        if (argumentStart < limit)
        {
            result.arguments.append(
                {argumentStart, limit});
        }
        return result;
    }

    bool isSingleStringLiteral(
        const TokenSpan& span,
        QString* decodedText) const
    {
        if (span.lastExclusive !=
                span.first + 1)
        {
            return false;
        }
        const LuaToken* token =
            tokenAt(span.first);
        if (token == nullptr ||
            (token->kind !=
                 LuaTokenKind::String &&
             token->kind !=
                 LuaTokenKind::LongString))
        {
            return false;
        }
        if (decodedText != nullptr)
            *decodedText = token->decodedText;
        return token->complete;
    }

    bool isSingleIntegerLiteral(
        const TokenSpan& span) const
    {
        if (span.lastExclusive ==
                span.first + 1)
        {
            const LuaToken* token =
                tokenAt(span.first);
            return token != nullptr &&
                token->kind ==
                    LuaTokenKind::Number;
        }
        if (span.lastExclusive ==
                span.first + 2 &&
            isTextAt(
                span.first,
                QStringLiteral("-")))
        {
            const LuaToken* token =
                tokenAt(span.first + 1);
            return token != nullptr &&
                token->kind ==
                    LuaTokenKind::Number;
        }
        return false;
    }

    bool singleIntegerLiteralValue(
        const TokenSpan& span,
        qint64& value) const
    {
        int numberIndex =
            span.first;
        bool negative = false;
        if (span.lastExclusive ==
                span.first + 2 &&
            isTextAt(
                span.first,
                QStringLiteral("-")))
        {
            negative = true;
            numberIndex = span.first + 1;
        }
        else if (span.lastExclusive !=
                 span.first + 1)
        {
            return false;
        }

        const LuaToken* token =
            tokenAt(numberIndex);
        if (token == nullptr ||
            token->kind != LuaTokenKind::Number)
        {
            return false;
        }
        QString digits = token->text;
        int base = 10;
        if (digits.startsWith(
                QStringLiteral("0x"),
                Qt::CaseInsensitive))
        {
            base = 16;
            digits.remove(0, 2);
        }
        if (digits.isEmpty() ||
            digits.contains(QLatin1Char('.')) ||
            digits.contains(QLatin1Char('e'),
                            Qt::CaseInsensitive) ||
            digits.contains(QLatin1Char('p'),
                            Qt::CaseInsensitive))
        {
            return false;
        }
        bool converted = false;
        const qulonglong magnitude =
            digits.toULongLong(
                &converted,
                base);
        if (!converted ||
            magnitude >
                static_cast<qulonglong>(
                    std::numeric_limits<qint64>::
                        max()))
        {
            return false;
        }
        value =
            negative
            ? -static_cast<qint64>(magnitude)
            : static_cast<qint64>(magnitude);
        return true;
    }

    QString trimAsciiWhitespace(
        const QString& value) const
    {
        int first = 0;
        int last = value.size();
        const auto isAsciiWhitespace =
            [](QChar character)
        {
            const ushort code =
                character.unicode();
            return code == 0x20 ||
                (code >= 0x09 &&
                 code <= 0x0d);
        };
        while (first < last &&
               isAsciiWhitespace(
                   value.at(first)))
        {
            ++first;
        }
        while (last > first &&
               isAsciiWhitespace(
                   value.at(last - 1)))
        {
            --last;
        }
        return value.mid(
            first,
            last - first);
    }

    bool argumentMatchesKind(
        const TokenSpan& span,
        StoryGraphArgumentValueKind kind) const
    {
        switch (kind)
        {
        case StoryGraphArgumentValueKind::Any:
            return span.isValid();
        case StoryGraphArgumentValueKind::String:
        case StoryGraphArgumentValueKind::VariableName:
            return isSingleStringLiteral(
                span,
                nullptr);
        case StoryGraphArgumentValueKind::Integer:
            return isSingleIntegerLiteral(span);
        case StoryGraphArgumentValueKind::Boolean:
            return span.lastExclusive ==
                    span.first + 1 &&
                (isTextAt(
                     span.first,
                     QStringLiteral("true")) ||
                 isTextAt(
                     span.first,
                     QStringLiteral("false")) ||
                 isSingleIntegerLiteral(span));
        }
        return false;
    }

    int matchingClosingToken(
        int openingTokenIndex,
        int limit,
        const QString& openingText,
        const QString& closingText)
    {
        int depth = 0;
        for (int index = openingTokenIndex;
             index < limit;
             ++index)
        {
            if (checkCancellation())
                return -1;
            const QString text = textAt(index);
            if (text == openingText)
                ++depth;
            else if (text == closingText)
            {
                --depth;
                if (depth == 0)
                    return index;
            }
        }
        return -1;
    }

    bool identifierStartsQualifiedCallee(
        int identifierIndex,
        int limit) const
    {
        int index = identifierIndex;
        while ((isTextAt(
                    index + 1,
                    QStringLiteral(".")) ||
                isTextAt(
                    index + 1,
                    QStringLiteral(":"))) &&
               tokenAt(index + 2) != nullptr &&
               tokenAt(index + 2)->kind ==
                   LuaTokenKind::Identifier)
        {
            index += 2;
        }
        if (index == identifierIndex)
            return false;
        return index + 1 < limit &&
            isCallSuffixStart(
                index + 1);
    }

    bool isOrdinaryVariableRead(
        int tokenIndex,
        int limit) const
    {
        const LuaToken* token =
            tokenAt(tokenIndex);
        if (token == nullptr ||
            token->kind !=
                LuaTokenKind::Identifier)
        {
            return false;
        }
        if (isTextAt(
                tokenIndex - 1,
                QStringLiteral(".")) ||
            isTextAt(
                tokenIndex - 1,
                QStringLiteral(":")) ||
            isTextAt(
                tokenIndex + 1,
                QStringLiteral("=")) ||
            identifierStartsQualifiedCallee(
                tokenIndex,
                limit))
        {
            return false;
        }
        if (StoryGraphSemanticCatalog::findExact(
                token->text) != nullptr ||
            StoryGraphRuntimeApiCatalog::findExact(
                token->text) != nullptr)
        {
            return false;
        }
        return true;
    }

    int indexedComputedCallOpening(
        int identifierIndex,
        int limit,
        QList<TokenSpan>& evaluatedIndexSpans)
    {
        int index = identifierIndex + 1;
        bool sawIndex = false;
        while (index < limit)
        {
            if (isTextAt(
                    index,
                    QStringLiteral("[")))
            {
                const int closingIndex =
                    matchingClosingToken(
                        index,
                        limit,
                        QStringLiteral("["),
                        QStringLiteral("]"));
                if (closingIndex < 0)
                    return -1;
                if (closingIndex > index + 1)
                {
                    evaluatedIndexSpans.append(
                        {index + 1, closingIndex});
                }
                index = closingIndex + 1;
                sawIndex = true;
                continue;
            }
            if ((isTextAt(
                     index,
                     QStringLiteral(".")) ||
                 isTextAt(
                     index,
                     QStringLiteral(":"))) &&
                tokenAt(index + 1) != nullptr &&
                tokenAt(index + 1)->kind ==
                    LuaTokenKind::Identifier)
            {
                index += 2;
                continue;
            }
            break;
        }
        if (!sawIndex ||
            !isCallSuffixStart(index))
        {
            return -1;
        }
        return index;
    }

    QString addComputedCallNode(
        const TokenSpan& callSpan,
        bool callComplete,
        ParserScope& scope,
        const QString& controlNodeId)
    {
        const StoryGraphCertainty certainty =
            callComplete
            ? StoryGraphCertainty::Dynamic
            : StoryGraphCertainty::Warning;
        const QString nodeId =
            addNode(
                *semanticGraph,
                StoryGraphNodeKind::DynamicCall,
                certainty,
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "计算调用"),
                displayForSpan(callSpan),
                scope.id,
                QStringLiteral("computed-call:") +
                    fingerprintForSpan(callSpan),
                callSpan);
        StoryGraphNode& node =
            semanticGraph->nodes.last();
        node.controlFlowNodeId =
            controlNodeId;
        appendSemanticNodeForControl(
            controlNodeId,
            nodeId);
        addWarning(
            StoryGraphWarningCategory::Semantic,
            StoryGraphWarningSeverity::Warning,
            QStringLiteral(
                "story_graph.semantic.computed_callee"),
            QCoreApplication::translate(
                "StoryGraphAnalyzer",
                "计算得到的 Lua 调用目标无法静态确认"),
            rangeForSpan(callSpan),
            nodeId,
            semanticGraph);
        return nodeId;
    }

    int emitComputedCallChain(
        int calleeStart,
        int openingTokenIndex,
        int limit,
        ParserScope& scope,
        const QString& controlNodeId,
        bool includeOrdinaryVariableReads)
    {
        int suffixIndex =
            openingTokenIndex;
        int consumedEnd =
            openingTokenIndex;
        while (suffixIndex < limit)
        {
            if (checkCancellation())
                return limit;
            if ((isTextAt(
                     suffixIndex,
                     QStringLiteral(".")) ||
                 isTextAt(
                     suffixIndex,
                     QStringLiteral(":"))) &&
                tokenAt(suffixIndex + 1) !=
                    nullptr &&
                tokenAt(suffixIndex + 1)->kind ==
                    LuaTokenKind::Identifier)
            {
                suffixIndex += 2;
                consumedEnd = suffixIndex;
                continue;
            }
            if (isTextAt(
                    suffixIndex,
                    QStringLiteral("[")))
            {
                const int closingIndex =
                    matchingClosingToken(
                        suffixIndex,
                        limit,
                        QStringLiteral("["),
                        QStringLiteral("]"));
                if (closingIndex < 0)
                    return limit;
                if (closingIndex >
                    suffixIndex + 1)
                {
                    extractCallsInRange(
                        suffixIndex + 1,
                        closingIndex,
                        scope,
                        controlNodeId,
                        includeOrdinaryVariableReads);
                    if (cancelled)
                        return limit;
                }
                suffixIndex =
                    closingIndex + 1;
                consumedEnd = suffixIndex;
                continue;
            }
            if (!isCallSuffixStart(
                    suffixIndex))
            {
                break;
            }
            const CallArguments parsed =
                parseCallArguments(
                    suffixIndex,
                    limit);
            for (const TokenSpan& argument :
                 parsed.arguments)
            {
                extractCallsInRange(
                    argument.first,
                    argument.lastExclusive,
                    scope,
                    controlNodeId,
                    includeOrdinaryVariableReads);
                if (cancelled)
                    return limit;
            }
            const int callEnd =
                parsed.closingTokenIndex >= 0
                ? parsed.closingTokenIndex + 1
                : limit;
            addComputedCallNode(
                {calleeStart, callEnd},
                parsed.complete,
                scope,
                controlNodeId);
            if (!parsed.complete)
                return limit;
            suffixIndex = callEnd;
            consumedEnd = callEnd;
        }
        return consumedEnd;
    }

    void extractCalls(
        const TokenSpan& span,
        ParserScope& scope,
        const QString& controlNodeId)
    {
        extractCallsInRange(
            std::max(0, span.first),
            std::min(
                tokenCount(),
                span.lastExclusive),
            scope,
            controlNodeId,
            false);
    }

    void extractCallsInRange(
        int first,
        int last,
        ParserScope& scope,
        const QString& controlNodeId,
        bool includeOrdinaryVariableReads =
            false)
    {
        for (int index = first;
             index < last;
             )
        {
            if (checkCancellation())
                return;
            if (isTextAt(
                    index,
                    QStringLiteral("function")))
            {
                index = skipFunctionExpression(
                    index,
                    last);
                continue;
            }

            if (isTextAt(
                    index,
                    QStringLiteral("(")))
            {
                const int calleeClosingIndex =
                    matchingClosingToken(
                        index,
                        last,
                        QStringLiteral("("),
                        QStringLiteral(")"));
                if (calleeClosingIndex >= 0 &&
                    isPostfixSuffixStart(
                        calleeClosingIndex + 1))
                {
                    extractCallsInRange(
                        index + 1,
                        calleeClosingIndex,
                        scope,
                        controlNodeId,
                        false);
                    if (cancelled)
                        return;
                    const int computedCallEnd =
                        emitComputedCallChain(
                            index,
                            calleeClosingIndex + 1,
                            last,
                            scope,
                            controlNodeId,
                            includeOrdinaryVariableReads);
                    index = std::max(
                        index + 1,
                        computedCallEnd);
                    continue;
                }
            }

            const LuaToken* token =
                tokenAt(index);
            if (token == nullptr ||
                token->kind !=
                    LuaTokenKind::Identifier)
            {
                ++index;
                continue;
            }
            if (isTextAt(
                    index - 1,
                    QStringLiteral("function")))
            {
                ++index;
                continue;
            }

            QList<TokenSpan> evaluatedIndexSpans;
            const int computedOpeningIndex =
                indexedComputedCallOpening(
                    index,
                    last,
                    evaluatedIndexSpans);
            if (computedOpeningIndex >= 0)
            {
                for (const TokenSpan& evaluatedSpan :
                     evaluatedIndexSpans)
                {
                    extractCallsInRange(
                        evaluatedSpan.first,
                        evaluatedSpan.lastExclusive,
                        scope,
                        controlNodeId,
                        includeOrdinaryVariableReads);
                    if (cancelled)
                        return;
                }
                const int computedCallEnd =
                    emitComputedCallChain(
                        index,
                        computedOpeningIndex,
                        last,
                        scope,
                        controlNodeId,
                        includeOrdinaryVariableReads);
                index = std::max(
                    index + 1,
                    computedCallEnd);
                continue;
            }

            QList<TokenSpan> arguments;
            int callEnd = index + 1;
            bool callComplete = true;
            if (isCallSuffixStart(
                    index + 1))
            {
                const CallArguments parsed =
                    parseCallArguments(
                        index + 1,
                        last);
                arguments = parsed.arguments;
                callComplete = parsed.complete;
                callEnd =
                    parsed.closingTokenIndex >= 0
                    ? parsed.closingTokenIndex + 1
                    : last;
            }
            else
            {
                if (includeOrdinaryVariableReads &&
                    isOrdinaryVariableRead(
                        index,
                        last))
                {
                    addOrdinaryVariableNode(
                        StoryGraphNodeKind::VariableRead,
                        StoryGraphCertainty::Certain,
                        token->text,
                        {index, index + 1},
                        scope,
                        controlNodeId,
                        QStringLiteral(
                            "lua-variable-read"));
                }
                ++index;
                continue;
            }

            for (const TokenSpan& argument :
                 arguments)
            {
                extractCallsInRange(
                    argument.first,
                    argument.lastExclusive,
                    scope,
                    controlNodeId,
                    includeOrdinaryVariableReads);
                if (cancelled)
                    return;
            }

            const bool qualified =
                isTextAt(
                    index - 1,
                    QStringLiteral(".")) ||
                isTextAt(
                    index - 1,
                    QStringLiteral(":"));
            addSemanticCall(
                token->text,
                {index, callEnd},
                arguments,
                qualified,
                callComplete,
                scope,
                controlNodeId);
            int completeCallEnd =
                callEnd;
            if (isPostfixSuffixStart(
                    callEnd))
            {
                completeCallEnd =
                    emitComputedCallChain(
                        index,
                        callEnd,
                        last,
                        scope,
                        controlNodeId,
                        includeOrdinaryVariableReads);
            }
            index = std::max(
                index + 1,
                completeCallEnd);
        }
    }

    int skipFunctionExpression(
        int functionTokenIndex,
        int limit)
    {
        QList<QString> blockClosers = {
            QStringLiteral("end")};
        QList<bool> awaitingDo = {false};
        for (int index = functionTokenIndex + 1;
             index < limit;
             ++index)
        {
            if (checkCancellation())
                return limit;
            const QString text = textAt(index);
            if (!blockClosers.isEmpty() &&
                text == blockClosers.constLast())
            {
                blockClosers.removeLast();
                awaitingDo.removeLast();
                if (blockClosers.isEmpty())
                    return index + 1;
                continue;
            }
            if (text == QStringLiteral("function") ||
                text == QStringLiteral("if"))
            {
                blockClosers.append(
                    QStringLiteral("end"));
                awaitingDo.append(false);
            }
            else if (text == QStringLiteral("while") ||
                     text == QStringLiteral("for"))
            {
                blockClosers.append(
                    QStringLiteral("end"));
                awaitingDo.append(true);
            }
            else if (text == QStringLiteral("repeat"))
            {
                blockClosers.append(
                    QStringLiteral("until"));
                awaitingDo.append(false);
            }
            else if (text == QStringLiteral("do"))
            {
                if (!awaitingDo.isEmpty() &&
                    awaitingDo.constLast())
                {
                    awaitingDo.last() = false;
                }
                else
                {
                    blockClosers.append(
                        QStringLiteral("end"));
                    awaitingDo.append(false);
                }
            }
        }
        return limit;
    }

    void addSemanticCall(
        const QString& callName,
        const TokenSpan& callSpan,
        const QList<TokenSpan>& arguments,
        bool qualified,
        bool callComplete,
        ParserScope& scope,
        const QString& controlNodeId)
    {
        const StoryGraphSemanticDefinition*
            definition =
                StoryGraphSemanticCatalog::findExact(
                    callName);
        const StoryGraphRuntimeApiDefinition*
            runtimeDefinition =
                StoryGraphRuntimeApiCatalog::findExact(
                    callName);
        StoryGraphNodeKind nodeKind =
            StoryGraphNodeKind::UnknownCall;
        StoryGraphCertainty certainty =
            StoryGraphCertainty::Unknown;
        QString title = callName;
        QString summary =
            displayForSpan(callSpan);
        QString literalTarget;
        QString variableName;
        QList<QString> indexedVariableNames;
        StoryGraphCertainty indexedVariableCertainty =
            StoryGraphCertainty::Dynamic;
        TokenSpan indexedVariableSourceSpan;
        QString indexedOutputDiagnosticCode;
        QString indexedOutputMessage;
        const StoryGraphCallSignature* signature =
            definition != nullptr
            ? definition->
                signatureForArgumentCount(
                    arguments.size())
            : nullptr;

        const bool shadowed =
            scope.visibleLocalNames.contains(
                callName) ||
            scope.mutatedGlobalNames.contains(
                callName) ||
            scope.visibleLocalNames.contains(
                QStringLiteral("_ENV")) ||
            scope.mutatedGlobalNames.contains(
                QStringLiteral("_ENV"));
        const bool directGlobal =
            !qualified && !shadowed;
        const QString lowerName =
            callName.toLower();
        const bool caseMismatch =
            directGlobal &&
            lowerName != callName &&
            StoryGraphRuntimeApiCatalog::
                containsExact(lowerName);
        if (definition != nullptr &&
            directGlobal)
        {
            nodeKind = semanticNodeKind(
                definition->category,
                definition->scriptCallKind);
            certainty =
                StoryGraphCertainty::Certain;
            if (signature == nullptr ||
                !callComplete)
            {
                certainty =
                    StoryGraphCertainty::Warning;
            }
            else
            {
                for (const
                     StoryGraphArgumentRoleBinding&
                         binding :
                     signature->argumentRoles)
                {
                    int firstArgumentIndex = -1;
                    int lastArgumentIndex = -1;
                    if (!binding.resolveRange(
                            arguments.size(),
                            firstArgumentIndex,
                            lastArgumentIndex))
                    {
                        continue;
                    }
                    for (int argumentIndex =
                             firstArgumentIndex;
                         argumentIndex <=
                             lastArgumentIndex;
                         ++argumentIndex)
                    {
                        if (!argumentMatchesKind(
                                arguments.at(
                                    argumentIndex),
                                binding.valueKind))
                        {
                            certainty =
                                StoryGraphCertainty::Dynamic;
                        }
                    }
                }
            }

            const StoryGraphArgumentRole targetRole =
                definition->category ==
                        StoryGraphSemanticCategory::ScriptCall
                ? StoryGraphArgumentRole::ScriptTarget
                : StoryGraphArgumentRole::MapTarget;
            if (signature != nullptr)
            {
                for (const
                     StoryGraphArgumentRoleBinding&
                         binding :
                     signature->argumentRoles)
                {
                    int firstArgumentIndex = -1;
                    int lastArgumentIndex = -1;
                    if (!binding.resolveRange(
                            arguments.size(),
                            firstArgumentIndex,
                            lastArgumentIndex))
                    {
                        continue;
                    }
                    if (binding.role == targetRole)
                    {
                        isSingleStringLiteral(
                            arguments.at(
                                firstArgumentIndex),
                            &literalTarget);
                    }
                    if (binding.role ==
                            StoryGraphArgumentRole::
                                VariableReadName ||
                        binding.role ==
                            StoryGraphArgumentRole::
                                VariableWriteName ||
                        binding.role ==
                            StoryGraphArgumentRole::
                                VariableReadWriteName ||
                        binding.role ==
                            StoryGraphArgumentRole::
                                ChoiceResultVariable ||
                        binding.role ==
                            StoryGraphArgumentRole::
                                ChoiceMultipleResultBase)
                    {
                        isSingleStringLiteral(
                            arguments.at(
                                firstArgumentIndex),
                            &variableName);
                    }
                }
            }
        }
        else if ((definition != nullptr ||
                  runtimeDefinition != nullptr) &&
                 !directGlobal)
        {
            nodeKind =
                StoryGraphNodeKind::DynamicCall;
            certainty =
                StoryGraphCertainty::Dynamic;
        }
        else if (runtimeDefinition != nullptr)
        {
            nodeKind =
                StoryGraphNodeKind::RegisteredApiCall;
            certainty =
                callComplete
                ? StoryGraphCertainty::Certain
                : StoryGraphCertainty::Warning;
        }
        else
        {
            if (!directGlobal)
            {
                nodeKind =
                    StoryGraphNodeKind::DynamicCall;
                certainty =
                    StoryGraphCertainty::Dynamic;
            }
            else if (caseMismatch)
            {
                certainty =
                    StoryGraphCertainty::Warning;
            }
            else if (!request.includeUnknownCalls)
            {
                return;
            }
        }

        if (definition != nullptr &&
            signature != nullptr)
        {
            const StoryGraphArgumentRole targetRole =
                definition->category ==
                        StoryGraphSemanticCategory::ScriptCall
                ? StoryGraphArgumentRole::ScriptTarget
                : StoryGraphArgumentRole::MapTarget;
            for (const
                 StoryGraphArgumentRoleBinding&
                     binding :
                 signature->argumentRoles)
            {
                int firstArgumentIndex = -1;
                int lastArgumentIndex = -1;
                if (!binding.resolveRange(
                        arguments.size(),
                        firstArgumentIndex,
                        lastArgumentIndex))
                {
                    continue;
                }
                if (binding.role == targetRole)
                {
                    isSingleStringLiteral(
                        arguments.at(
                            firstArgumentIndex),
                        &literalTarget);
                }
                if (binding.role ==
                        StoryGraphArgumentRole::
                            VariableReadName ||
                    binding.role ==
                        StoryGraphArgumentRole::
                            VariableWriteName ||
                    binding.role ==
                        StoryGraphArgumentRole::
                            VariableReadWriteName ||
                    binding.role ==
                        StoryGraphArgumentRole::
                            ChoiceResultVariable ||
                    binding.role ==
                        StoryGraphArgumentRole::
                            ChoiceMultipleResultBase)
                {
                    isSingleStringLiteral(
                        arguments.at(
                            firstArgumentIndex),
                        &variableName);
                }
            }
        }

        if (definition != nullptr &&
            definition->indexedVariableOutput.enabled)
        {
            variableName.clear();
            if (directGlobal &&
                signature != nullptr &&
                callComplete)
            {
                const int baseArgumentIndex =
                    definition->
                        indexedVariableOutput.
                            baseArgument.resolve(
                                arguments.size());
                const int countArgumentIndex =
                    definition->
                        indexedVariableOutput.
                            outputCountArgument.resolve(
                                arguments.size());
                if (baseArgumentIndex >= 0 &&
                    baseArgumentIndex <
                        arguments.size() &&
                    countArgumentIndex >= 0 &&
                    countArgumentIndex <
                        arguments.size())
                {
                    QString outputBase;
                    const bool literalBase =
                        isSingleStringLiteral(
                            arguments.at(
                                baseArgumentIndex),
                            &outputBase);
                    qint64 outputCount = 0;
                    const bool literalCount =
                        singleIntegerLiteralValue(
                            arguments.at(
                                countArgumentIndex),
                            outputCount);
                    const TokenSpan countSpan =
                        arguments.at(
                            countArgumentIndex);
                    const LuaToken* unaryPlusToken =
                        countSpan.lastExclusive ==
                                countSpan.first + 2 &&
                            isTextAt(
                                countSpan.first,
                                QStringLiteral("+"))
                        ? tokenAt(
                              countSpan.first + 1)
                        : nullptr;
                    const bool countLooksLiteral =
                        isSingleIntegerLiteral(
                            countSpan) ||
                        (unaryPlusToken != nullptr &&
                         unaryPlusToken->kind ==
                             LuaTokenKind::Number);
                    if (!literalCount)
                    {
                        certainty =
                            countLooksLiteral
                            ? StoryGraphCertainty::Warning
                            : StoryGraphCertainty::Dynamic;
                        indexedOutputDiagnosticCode =
                            countLooksLiteral
                            ? QStringLiteral(
                                  "story_graph.semantic.choice_multiple_invalid_output_count")
                            : QStringLiteral(
                                  "story_graph.semantic.choice_multiple_dynamic_output_count");
                        indexedOutputMessage =
                            countLooksLiteral
                            ? QCoreApplication::translate(
                                  "StoryGraphAnalyzer",
                                  "choosemultiple 的选择数量字面量不是受支持的整数")
                            : QCoreApplication::translate(
                                  "StoryGraphAnalyzer",
                                  "choosemultiple 的动态选择数量无法展开变量写入");
                    }
                    else if (outputCount <= 0)
                    {
                        certainty =
                            StoryGraphCertainty::Warning;
                        indexedOutputDiagnosticCode =
                            QStringLiteral(
                                "story_graph.semantic.choice_multiple_invalid_output_count");
                        indexedOutputMessage =
                            QCoreApplication::translate(
                                "StoryGraphAnalyzer",
                                "choosemultiple 的选择数量必须为正整数");
                    }
                    else if (outputCount >
                             ChoiceMultipleGeneratedVariableLimit)
                    {
                        certainty =
                            StoryGraphCertainty::Warning;
                        indexedOutputDiagnosticCode =
                            QStringLiteral(
                                "story_graph.semantic.choice_multiple_output_limit");
                        indexedOutputMessage =
                            QCoreApplication::translate(
                                "StoryGraphAnalyzer",
                                "choosemultiple 的变量写入数量超过静态展开上限");
                    }
                    else if (!literalBase)
                    {
                        certainty =
                            StoryGraphCertainty::Dynamic;
                        indexedOutputDiagnosticCode =
                            QStringLiteral(
                                "story_graph.semantic.choice_multiple_dynamic_output_base");
                        indexedOutputMessage =
                            QCoreApplication::translate(
                                "StoryGraphAnalyzer",
                                "choosemultiple 的动态变量基名无法展开");
                    }
                    else
                    {
                        if (definition->
                                indexedVariableOutput.
                                    trimAsciiWhitespace)
                        {
                            outputBase =
                                trimAsciiWhitespace(
                                    outputBase);
                        }
                        indexedVariableSourceSpan =
                            arguments.at(
                                baseArgumentIndex);
                        qint64 generatedOutputCount =
                            outputCount;
                        constexpr int
                            choiceMultipleFixedArgumentCount =
                                4;
                        const qint64 optionSlotCount =
                            std::max<qint64>(
                                0,
                                static_cast<qint64>(
                                    arguments.size()) -
                                    choiceMultipleFixedArgumentCount);
                        if (outputCount >
                            optionSlotCount)
                        {
                            certainty =
                                StoryGraphCertainty::Warning;
                            indexedOutputDiagnosticCode =
                                QStringLiteral(
                                    "story_graph.semantic.choice_multiple_selection_count_exceeds_options");
                            indexedOutputMessage =
                                QCoreApplication::translate(
                                    "StoryGraphAnalyzer",
                                    "choosemultiple 的选择数量超过可用选项槽位");
                            generatedOutputCount =
                                optionSlotCount;
                        }
                        for (qint64 outputIndex = 0;
                             outputIndex <
                                 generatedOutputCount;
                             ++outputIndex)
                        {
                            QString generatedName =
                                definition->
                                    indexedVariableOutput.
                                        generatedNamePrefix +
                                outputBase;
                            if (definition->
                                    indexedVariableOutput.
                                        appendDecimalIndex)
                            {
                                generatedName +=
                                    QString::number(
                                        definition->
                                            indexedVariableOutput.
                                                firstIndex +
                                        outputIndex);
                            }
                            indexedVariableNames.append(
                                generatedName);
                        }
                    }
                }
            }
        }

        const QString fingerprint =
            QStringLiteral("call:") +
            callName +
            QLatin1Char(':') +
            fingerprintForSpan(callSpan);
        const QString semanticNodeId =
            addNode(
                *semanticGraph,
                nodeKind,
                certainty,
                title,
                summary,
                scope.id,
                fingerprint,
                callSpan);
        StoryGraphNode* semanticNode =
            nullptr;
        if (!semanticGraph->nodes.isEmpty())
        {
            semanticNode =
                &semanticGraph->nodes.last();
            semanticNode->controlFlowNodeId =
                controlNodeId;
            semanticNode->apiName = callName;
            semanticNode->literalTarget =
                literalTarget;
            semanticNode->variableName =
                variableName;
        }
        appendSemanticNodeForControl(
            controlNodeId,
            semanticNodeId);

        for (const QString& indexedVariableName :
             indexedVariableNames)
        {
            if (checkCancellation())
                return;
            addOrdinaryVariableNode(
                StoryGraphNodeKind::VariableWrite,
                indexedVariableCertainty,
                indexedVariableName,
                indexedVariableSourceSpan,
                scope,
                controlNodeId,
                QStringLiteral(
                    "choosemultiple-variable-write"));
        }
        if (!indexedOutputDiagnosticCode.isEmpty())
        {
            addWarning(
                StoryGraphWarningCategory::Semantic,
                StoryGraphWarningSeverity::Warning,
                indexedOutputDiagnosticCode,
                indexedOutputMessage,
                rangeForSpan(callSpan),
                semanticNodeId,
                semanticGraph);
        }

        if (caseMismatch)
        {
            addWarning(
                StoryGraphWarningCategory::Semantic,
                StoryGraphWarningSeverity::Warning,
                QStringLiteral(
                    "story_graph.semantic.api_case_mismatch"),
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "Lua API 调用大小写与运行时注册名不一致"),
                rangeForSpan(callSpan),
                semanticNodeId,
                semanticGraph);
        }
        if (!directGlobal)
        {
            const bool knownRuntimeName =
                definition != nullptr ||
                runtimeDefinition != nullptr;
            addWarning(
                StoryGraphWarningCategory::Semantic,
                StoryGraphWarningSeverity::Warning,
                knownRuntimeName
                    ? QStringLiteral(
                          "story_graph.semantic.api_not_direct_global")
                    : QStringLiteral(
                          "story_graph.semantic.dynamic_callee"),
                knownRuntimeName
                    ? QCoreApplication::translate(
                          "StoryGraphAnalyzer",
                          "已注册 API 不是可确认的直接全局调用")
                    : QCoreApplication::translate(
                          "StoryGraphAnalyzer",
                          "Lua 调用目标来自字段、别名或动态环境"),
                rangeForSpan(callSpan),
                semanticNodeId,
                semanticGraph);
        }
        else if (definition != nullptr &&
                 (signature == nullptr ||
                  !callComplete))
        {
            addWarning(
                StoryGraphWarningCategory::Semantic,
                StoryGraphWarningSeverity::Warning,
                QStringLiteral(
                    "story_graph.semantic.signature_mismatch"),
                QCoreApplication::translate(
                    "StoryGraphAnalyzer",
                    "调用参数数量或括号与当前运行时签名不一致"),
                rangeForSpan(callSpan),
                semanticNodeId,
                semanticGraph);
        }
        else if (definition != nullptr &&
                 signature != nullptr)
        {
            const int ignoredCount =
                signature->
                    ignoredTrailingArgumentCount(
                        arguments.size());
            if (ignoredCount > 0)
            {
                addWarning(
                    StoryGraphWarningCategory::Semantic,
                    StoryGraphWarningSeverity::Information,
                    QStringLiteral(
                        "story_graph.semantic.ignored_trailing_arguments"),
                    QCoreApplication::translate(
                        "StoryGraphAnalyzer",
                        "运行时会忽略该调用的尾随参数"),
                    rangeForSpan(callSpan),
                    semanticNodeId,
                    semanticGraph);
            }
            if (definition->optionConditionPolicy ==
                    StoryGraphOptionConditionPolicy::
                        RuntimeMicroSyntaxUnexpanded &&
                optionConditionPresent(
                    *definition,
                    *signature,
                    arguments))
            {
                addWarning(
                    StoryGraphWarningCategory::Semantic,
                    StoryGraphWarningSeverity::Information,
                    QStringLiteral(
                        "story_graph.semantic.choice_condition_unexpanded"),
                    QCoreApplication::translate(
                        "StoryGraphAnalyzer",
                        "选择项中的运行时条件微语法尚未展开"),
                    rangeForSpan(callSpan),
                    semanticNodeId,
                    semanticGraph);
            }
        }
    }

    void appendSemanticNodeForControl(
        const QString& controlNodeId,
        const QString& semanticNodeId)
    {
        if (controlNodeId.isEmpty() ||
            semanticNodeId.isEmpty())
        {
            return;
        }
        if (!semanticNodeIdsByControlNode.contains(
                controlNodeId))
        {
            semanticControlNodeOrder.append(
                controlNodeId);
        }
        semanticNodeIdsByControlNode[
            controlNodeId].append(
                semanticNodeId);
    }

    static int certaintyRank(
        StoryGraphCertainty certainty)
    {
        switch (certainty)
        {
        case StoryGraphCertainty::Certain:
            return 0;
        case StoryGraphCertainty::Dynamic:
            return 1;
        case StoryGraphCertainty::Unknown:
            return 2;
        case StoryGraphCertainty::Warning:
            return 3;
        }
        return 3;
    }

    static StoryGraphCertainty combineCertainty(
        StoryGraphCertainty first,
        StoryGraphCertainty second)
    {
        return certaintyRank(first) >=
                certaintyRank(second)
            ? first
            : second;
    }

    static StoryGraphEdgeKind combineProjectionKind(
        StoryGraphEdgeKind current,
        StoryGraphEdgeKind next)
    {
        if (current == StoryGraphEdgeKind::Unknown ||
            current == StoryGraphEdgeKind::Dynamic)
        {
            return current;
        }
        if (next == StoryGraphEdgeKind::Unknown ||
            next == StoryGraphEdgeKind::Dynamic)
        {
            return next;
        }
        if (current ==
            StoryGraphEdgeKind::Sequential)
        {
            return next;
        }
        if (current ==
                StoryGraphEdgeKind::Fallthrough &&
            next !=
                StoryGraphEdgeKind::Sequential &&
            next !=
                StoryGraphEdgeKind::Fallthrough)
        {
            return next;
        }
        return current;
    }

    void buildSemanticEdges()
    {
        QHash<QString, QList<int>>
            outgoingControlEdgeIndexes;
        for (int edgeIndex = 0;
             edgeIndex < controlGraph->edges.size();
             ++edgeIndex)
        {
            if (checkCancellation())
                return;
            const StoryGraphEdge& edge =
                controlGraph->edges.at(edgeIndex);
            outgoingControlEdgeIndexes[
                edge.fromNodeId].append(
                    edgeIndex);
        }

        QHash<QString, StoryGraphCertainty>
            semanticNodeCertainties;
        for (const StoryGraphNode& node :
             semanticGraph->nodes)
        {
            if (checkCancellation())
                return;
            semanticNodeCertainties.insert(
                node.id,
                node.certainty);
        }

        QSet<QString> emittedEdges;
        const auto emitProjectedEdge =
            [this,
             &semanticNodeCertainties,
             &emittedEdges](
                const QString& fromNodeId,
                const QString& toNodeId,
                StoryGraphEdgeKind kind,
                StoryGraphCertainty certainty,
                const QString& fingerprint)
        {
            const QString edgeKey =
                fromNodeId +
                QLatin1Char('|') +
                toNodeId +
                QLatin1Char('|') +
                QString::number(
                    static_cast<int>(kind));
            if (emittedEdges.contains(edgeKey))
                return;
            emittedEdges.insert(edgeKey);
            certainty = combineCertainty(
                certainty,
                semanticNodeCertainties.value(
                    fromNodeId,
                    StoryGraphCertainty::Certain));
            certainty = combineCertainty(
                certainty,
                semanticNodeCertainties.value(
                    toNodeId,
                    StoryGraphCertainty::Certain));
            addEdge(
                *semanticGraph,
                fromNodeId,
                toNodeId,
                kind,
                certainty,
                fingerprint,
                storyGraphEdgeKindToString(kind));
        };

        for (const QString& controlNodeId :
             semanticControlNodeOrder)
        {
            if (checkCancellation())
                return;
            const QList<QString> semanticNodeIds =
                semanticNodeIdsByControlNode.value(
                    controlNodeId);
            for (int semanticIndex = 1;
                 semanticIndex <
                     semanticNodeIds.size();
                 ++semanticIndex)
            {
                emitProjectedEdge(
                    semanticNodeIds.at(
                        semanticIndex - 1),
                    semanticNodeIds.at(
                        semanticIndex),
                    StoryGraphEdgeKind::Sequential,
                    StoryGraphCertainty::Certain,
                    QStringLiteral(
                        "semantic-control-order"));
            }

            if (semanticNodeIds.isEmpty())
                continue;
            const QString sourceSemanticNodeId =
                semanticNodeIds.constLast();
            QList<SemanticProjectionState> pending;
            for (const int edgeIndex :
                 outgoingControlEdgeIndexes.value(
                     controlNodeId))
            {
                const StoryGraphEdge& edge =
                    controlGraph->edges.at(
                        edgeIndex);
                pending.append({
                    edge.toNodeId,
                    edge.kind,
                    edge.certainty});
            }

            QSet<QString> visitedStates;
            int pendingIndex = 0;
            while (pendingIndex < pending.size())
            {
                if (checkCancellation())
                    return;
                const SemanticProjectionState state =
                    pending.at(pendingIndex++);
                const QString stateKey =
                    state.controlNodeId +
                    QLatin1Char('|') +
                    QString::number(
                        static_cast<int>(
                            state.edgeKind)) +
                    QLatin1Char('|') +
                    QString::number(
                        static_cast<int>(
                            state.certainty));
                if (visitedStates.contains(
                        stateKey))
                {
                    continue;
                }
                visitedStates.insert(stateKey);

                const QList<QString> targetSemanticIds =
                    semanticNodeIdsByControlNode.value(
                        state.controlNodeId);
                if (!targetSemanticIds.isEmpty())
                {
                    emitProjectedEdge(
                        sourceSemanticNodeId,
                        targetSemanticIds.constFirst(),
                        state.edgeKind,
                        state.certainty,
                        QStringLiteral(
                            "semantic-cfg-projection:") +
                            storyGraphEdgeKindToString(
                                state.edgeKind));
                    continue;
                }

                for (const int edgeIndex :
                     outgoingControlEdgeIndexes.value(
                         state.controlNodeId))
                {
                    const StoryGraphEdge& edge =
                        controlGraph->edges.at(
                            edgeIndex);
                    pending.append({
                        edge.toNodeId,
                        combineProjectionKind(
                            state.edgeKind,
                            edge.kind),
                        combineCertainty(
                            state.certainty,
                            edge.certainty)});
                }
            }
        }
    }

    bool optionConditionPresent(
        const StoryGraphSemanticDefinition& definition,
        const StoryGraphCallSignature& signature,
        const QList<TokenSpan>& arguments) const
    {
        Q_UNUSED(definition);
        for (const
             StoryGraphArgumentRoleBinding& binding :
             signature.argumentRoles)
        {
            if (binding.role !=
                StoryGraphArgumentRole::ChoiceOption)
            {
                continue;
            }
            int firstArgumentIndex = -1;
            int lastArgumentIndex = -1;
            if (!binding.resolveRange(
                    arguments.size(),
                    firstArgumentIndex,
                    lastArgumentIndex))
            {
                continue;
            }
            for (int index =
                     firstArgumentIndex;
                 index <= lastArgumentIndex;
                 ++index)
            {
                QString text;
                if (isSingleStringLiteral(
                        arguments.at(index),
                        &text) &&
                    text.contains(
                        QLatin1Char('{')))
                {
                    return true;
                }
            }
        }
        return false;
    }

    const StoryGraphAnalysisRequest& request;
    StoryGraphDocumentResult& documentResult;
    StoryGraphAnalyzer::CancelCallback
        cancelCallback;
    QList<LuaToken> tokens;
    int cursor = 0;
    int operationCount = 0;
    int nextCancellationCheck = 0;
    bool cancelled = false;
    StoryGraphResult* controlGraph = nullptr;
    StoryGraphResult* semanticGraph = nullptr;
    QHash<QString, QList<QString>>
        semanticNodeIdsByControlNode;
    QList<QString> semanticControlNodeOrder;
    QHash<QString, int> nodeOccurrenceCounts;
    QHash<QString, int> edgeOccurrenceCounts;
    QHash<QString, int> functionOccurrenceCounts;
};

void appendLexWarnings(
    const LuaLexResult& lexResult,
    StoryGraphDocumentResult& documentResult)
{
    for (const LuaLexWarning& lexWarning :
         lexResult.warnings)
    {
        StoryGraphWarning warning;
        warning.category =
            StoryGraphWarningCategory::Lexical;
        warning.severity =
            StoryGraphWarningSeverity::Warning;
        warning.diagnosticCode =
            lexWarning.diagnosticCode;
        warning.message = lexWarning.message;
        warning.source = documentResult.source;
        warning.sourceRange =
            toStoryGraphRange(
                lexWarning.range);
        documentResult.warnings.append(
            warning);
        documentResult.controlFlowGraph.warnings.append(
            std::move(warning));
    }
    if (!lexResult.warnings.isEmpty())
    {
        documentResult.status =
            StoryGraphDocumentStatus::Partial;
        documentResult.controlFlowGraph.complete =
            false;
        documentResult.semanticGraph.complete =
            false;
    }
}
}

StoryGraphDocumentResult StoryGraphAnalyzer::analyze(
    const StoryGraphAnalysisRequest& request,
    const CancelCallback& cancelCallback)
{
    StoryGraphDocumentResult result;
    result.source = request.source.identity;
    result.analysisGeneration =
        request.analysisGeneration;
    result.controlFlowGraph.kind =
        StoryGraphKind::ControlFlow;
    result.semanticGraph.kind =
        StoryGraphKind::StorySemantics;
    if (cancelCallback &&
        cancelCallback())
    {
        result.status =
            StoryGraphDocumentStatus::Cancelled;
        result.controlFlowGraph.complete = false;
        result.semanticGraph.complete = false;
        return result;
    }
    if (result.source.contentSha256.isEmpty())
    {
        result.source.contentSha256 =
            QCryptographicHash::hash(
                request.source.utf8Bytes,
                QCryptographicHash::Sha256);
    }

    const LuaLexResult lexResult =
        LuaLexer::lexUtf8(
            request.source.utf8Bytes,
            result.source.virtualPath,
            cancelCallback);
    appendLexWarnings(
        lexResult,
        result);
    if (lexResult.cancelled)
    {
        result.status =
            StoryGraphDocumentStatus::Cancelled;
        result.controlFlowGraph.complete = false;
        result.semanticGraph.complete = false;
        return result;
    }
    if (!lexResult.warnings.isEmpty() &&
        lexResult.tokens.size() == 1)
    {
        result.status =
            StoryGraphDocumentStatus::Failed;
        result.controlFlowGraph.complete = false;
        result.semanticGraph.complete = false;
        return result;
    }

    Parser parser(
        request,
        lexResult,
        result,
        cancelCallback);
    parser.parse();
    return result;
}
