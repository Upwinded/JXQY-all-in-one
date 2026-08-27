#include "StoryGraphProjectResolver.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <exception>
#include <utility>

namespace
{
constexpr int MaximumTrackedMapContexts = 64;

struct CachedRead
{
    StoryGraphReadResult result;
    bool fromEditorBuffer = false;
    qint64 documentRevision = -1;
    QByteArray contentSha256;
    bool budgetExceeded = false;
    QString budgetDiagnosticCode;
    QString budgetMessage;
};

struct BranchResolution
{
    StoryGraphTargetResolutionStatus status =
        StoryGraphTargetResolutionStatus::Missing;
    StoryGraphMapContext context;
    QString candidateVirtualPath;
    StoryGraphSourceSnapshot source;
    QString message;
    QString diagnosticCode;
    bool contextDependent = false;
};

struct DocumentFlowIndex
{
    QHash<QString, int> nodeIndexes;
    QHash<QString, QStringList> successorNodeIds;
    QStringList detachedEntryNodeIds;
};

struct FlowContinuation
{
    QString callerInvocationKey;
    QStringList successorNodeIds;
};

struct InvocationState
{
    int documentIndex = -1;
    QString entryNodeId;
    QString scopeId;
    StoryGraphMapContext entryContext;
    int depth = 0;
    QList<StoryGraphMapContext> exitContexts;
    QSet<QString> exitContextKeys;
    QList<FlowContinuation> continuations;
    QSet<QString> continuationKeys;
};

struct PendingFlowState
{
    QString invocationKey;
    QString nodeId;
    StoryGraphMapContext context;
};

struct PendingResolutionRecord
{
    StoryGraphNode caller;
    BranchResolution branch;
    StoryGraphEdgeKind edgeKind =
        StoryGraphEdgeKind::Call;
    QString targetEntryNodeId;
    int depth = 0;
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

QString readKey(
    const StoryGraphContentRoot& root,
    const QString& virtualPath)
{
    return sourceKey(
        root.portableRootKey,
        virtualPath);
}

QString contextKey(
    const StoryGraphMapContext& context)
{
    return storyGraphMapContextStateToString(
               context.state) +
        QChar::Null +
        context.effectiveMapFolder;
}

bool isControlCharacter(
    ushort value)
{
    return value <= 0x1fU ||
        (value >= 0x7fU && value <= 0x9fU);
}

bool hasValidUnicode(
    const QString& value)
{
    for (qsizetype index = 0;
         index < value.size();
         ++index)
    {
        const QChar character = value.at(index);
        if (character.isHighSurrogate())
        {
            if (index + 1 >= value.size() ||
                !value.at(index + 1).
                    isLowSurrogate())
            {
                return false;
            }
            ++index;
        }
        else if (character.isLowSurrogate())
        {
            return false;
        }
    }
    return true;
}

QString asciiCaseFold(QString value)
{
    for (qsizetype index = 0;
         index < value.size();
         ++index)
    {
        const ushort character =
            value.at(index).unicode();
        if (character >=
                static_cast<ushort>('A') &&
            character <=
                static_cast<ushort>('Z'))
        {
            value[index] =
                QChar(
                    character +
                    static_cast<ushort>(
                        'a' - 'A'));
        }
    }
    return value;
}

bool isReservedWindowsPathComponent(
    const QString& segment)
{
    QString baseName =
        segment.section(QLatin1Char('.'), 0, 0);
    while (baseName.endsWith(QLatin1Char('.')) ||
           baseName.endsWith(QLatin1Char(' ')))
    {
        baseName.chop(1);
    }
    baseName = asciiCaseFold(baseName);
    if (baseName == QStringLiteral("con") ||
        baseName == QStringLiteral("prn") ||
        baseName == QStringLiteral("aux") ||
        baseName == QStringLiteral("nul") ||
        baseName == QStringLiteral("conin$") ||
        baseName == QStringLiteral("conout$"))
    {
        return true;
    }
    if (!baseName.startsWith(QStringLiteral("com")) &&
        !baseName.startsWith(QStringLiteral("lpt")))
    {
        return false;
    }
    if (baseName.size() != 4)
        return false;
    const QChar suffix = baseName.at(3);
    return (suffix >= QLatin1Char('1') &&
            suffix <= QLatin1Char('9')) ||
        suffix == QChar(0x00B9) ||
        suffix == QChar(0x00B2) ||
        suffix == QChar(0x00B3);
}

bool strictPath(
    const QString& value,
    QString* rejectionReason)
{
    auto reject =
        [rejectionReason](
            const QString& reason)
    {
        if (rejectionReason != nullptr)
            *rejectionReason = reason;
        return false;
    };

    if (rejectionReason != nullptr)
        rejectionReason->clear();
    if (value.isEmpty())
    {
        return reject(
            QStringLiteral("empty-path"));
    }
    if (!hasValidUnicode(value))
    {
        return reject(
            QStringLiteral("invalid-unicode"));
    }
    if (value.startsWith(QLatin1Char('/')) ||
        value.startsWith(QLatin1Char('\\')))
    {
        return reject(
            QStringLiteral("absolute-path"));
    }
    if (value.contains(QLatin1Char('\\')))
    {
        return reject(
            QStringLiteral("backslash"));
    }
    if (value.contains(QLatin1Char(':')))
    {
        return reject(
            QStringLiteral("colon"));
    }
    for (const QChar character : value)
    {
        if (isControlCharacter(
                character.unicode()))
        {
            return reject(
                QStringLiteral(
                    "control-character"));
        }
    }

    const QStringList segments =
        value.split(
            QLatin1Char('/'),
            Qt::KeepEmptyParts);
    for (const QString& segment : segments)
    {
        if (segment.isEmpty())
        {
            return reject(
                QStringLiteral("empty-segment"));
        }
        if (segment == QStringLiteral(".."))
        {
            return reject(
                QStringLiteral(
                    "parent-segment"));
        }
        if (segment == QStringLiteral("."))
        {
            return reject(
                QStringLiteral(
                    "current-segment"));
        }
        if (segment.endsWith(QLatin1Char('.')) ||
            segment.endsWith(QLatin1Char(' ')))
        {
            return reject(
                QStringLiteral(
                    "trailing-dot-or-space"));
        }
        for (const QChar character : segment)
        {
            if (character == QLatin1Char('<') ||
                character == QLatin1Char('>') ||
                character == QLatin1Char('"') ||
                character == QLatin1Char('|') ||
                character == QLatin1Char('?') ||
                character == QLatin1Char('*'))
            {
                return reject(
                    QStringLiteral(
                        "invalid-character"));
            }
        }
        if (isReservedWindowsPathComponent(
                segment))
        {
            return reject(
                QStringLiteral(
                    "reserved-windows-name"));
        }
    }
    return true;
}

QString mapBasename(
    const QString& strictMapTarget)
{
    const qsizetype slash =
        strictMapTarget.lastIndexOf(
            QLatin1Char('/'));
    QString basename =
        strictMapTarget.mid(slash + 1);
    const qsizetype dot =
        basename.lastIndexOf(
            QLatin1Char('.'));
    if (dot > 0)
        basename.truncate(dot);
    return basename;
}

QList<StoryGraphMapContext> normalizeContexts(
    const QList<StoryGraphMapContext>& source,
    bool* collapsed)
{
    if (collapsed != nullptr)
        *collapsed = false;

    QList<StoryGraphMapContext> result;
    QSet<QString> seen;
    for (const StoryGraphMapContext& context :
         source)
    {
        StoryGraphMapContext normalized =
            context;
        if (normalized.state ==
            StoryGraphMapContextState::Unknown)
        {
            normalized.effectiveMapFolder.clear();
        }
        const QString key =
            contextKey(normalized);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        result.append(std::move(normalized));
        if (result.size() >
            MaximumTrackedMapContexts)
        {
            StoryGraphMapContext unknown;
            unknown.state =
                StoryGraphMapContextState::Unknown;
            result.clear();
            result.append(unknown);
            if (collapsed != nullptr)
                *collapsed = true;
            break;
        }
    }
    if (result.isEmpty())
    {
        StoryGraphMapContext unknown;
        unknown.state =
            StoryGraphMapContextState::Unknown;
        result.append(unknown);
    }
    return result;
}

void markPartial(
    StoryGraphProjectResult& result)
{
    if (result.status ==
            StoryGraphProjectStatus::Complete)
    {
        result.status =
            StoryGraphProjectStatus::Partial;
    }
}

void appendWarning(
    StoryGraphProjectResult& result,
    StoryGraphWarningSeverity severity,
    const QString& diagnosticCode,
    const QString& message,
    const StoryGraphSourceIdentity& source =
        StoryGraphSourceIdentity(),
    const StoryGraphSourceRange& sourceRange =
        StoryGraphSourceRange(),
    const QString& relatedNodeId = QString(),
    bool appendToSemanticGraph = true)
{
    StoryGraphWarning warning;
    warning.category =
        diagnosticCode.startsWith(
            QStringLiteral(
                "story_graph.project.budget"))
        ? StoryGraphWarningCategory::Budget
        : diagnosticCode ==
                QStringLiteral(
                    "story_graph.project.cancelled")
            ? StoryGraphWarningCategory::
                Cancellation
            : StoryGraphWarningCategory::
                Resolution;
    warning.severity = severity;
    warning.diagnosticCode = diagnosticCode;
    warning.message = message;
    warning.source = source;
    warning.sourceRange = sourceRange;
    warning.relatedNodeId = relatedNodeId;
    result.warnings.append(warning);
    if (appendToSemanticGraph)
    {
        result.semanticGraph.warnings.append(
            std::move(warning));
    }
    if (severity !=
        StoryGraphWarningSeverity::Information)
    {
        markPartial(result);
    }
}

void appendGraph(
    StoryGraphResult& target,
    const StoryGraphResult& source,
    QSet<QString>& nodeIds,
    QSet<QString>& edgeIds)
{
    for (const StoryGraphNode& node :
         source.nodes)
    {
        if (nodeIds.contains(node.id))
            continue;
        nodeIds.insert(node.id);
        target.nodes.append(node);
    }
    for (const StoryGraphEdge& edge :
         source.edges)
    {
        if (edgeIds.contains(edge.id))
            continue;
        edgeIds.insert(edge.id);
        target.edges.append(edge);
    }
    for (const StoryGraphWarning& warning :
         source.warnings)
    {
        target.warnings.append(warning);
    }
    target.complete =
        target.complete && source.complete;
}

QString resolutionFingerprint(
    StoryGraphTargetResolutionStatus status,
    const StoryGraphNode& caller,
    const QString& literalTarget,
    const StoryGraphMapContext& context,
    const QString& candidateVirtualPath,
    const QString& targetPortableRootKey,
    const QString& targetVirtualPath)
{
    return QStringLiteral("project-resolution-v1|") +
        storyGraphTargetResolutionStatusToString(
            status) +
        QLatin1Char('|') +
        caller.id +
        QLatin1Char('|') +
        literalTarget +
        QLatin1Char('|') +
        storyGraphMapContextStateToString(
            context.state) +
        QLatin1Char('|') +
        context.effectiveMapFolder +
        QLatin1Char('|') +
        candidateVirtualPath +
        QLatin1Char('|') +
        targetPortableRootKey +
        QLatin1Char('|') +
        targetVirtualPath;
}

class Resolver
{
public:
    Resolver(
        const StoryGraphProjectRequest& request,
        const StoryGraphProjectResolver::ReadCallback&
            readCallback,
        const StoryGraphProjectResolver::
            MapFolderResolverCallback&
                mapFolderResolver,
        const StoryGraphProjectResolver::
            CancelCallback&
                cancelCallback)
        : request(request)
        , readCallback(readCallback)
        , mapFolderResolver(mapFolderResolver)
        , cancelCallback(cancelCallback)
    {
        result.analysisGeneration =
            request.analysisGeneration;
        result.controlFlowGraph.kind =
            StoryGraphKind::ControlFlow;
        result.semanticGraph.kind =
            StoryGraphKind::StorySemantics;
    }

    StoryGraphProjectResult run()
    {
        if (!validateRequest())
            return result;

        if (isCancelled())
        {
            cancelProject();
            return result;
        }

        QList<StoryGraphMapContext> entryContexts;
        entryContexts.append(
            request.entryMapContext);
        bool collapsed = false;
        entryContexts =
            normalizeContexts(
                entryContexts,
                &collapsed);

        const int entryDocumentIndex =
            ensureDocument(
                preparedEntrySource,
                entryContexts);
        if (entryDocumentIndex < 0)
        {
            if (result.status ==
                StoryGraphProjectStatus::Complete)
            {
                result.status =
                    StoryGraphProjectStatus::Failed;
            }
            return result;
        }

        const StoryGraphDocumentResult& entry =
            result.documents.at(
                entryDocumentIndex);
        result.controlFlowGraph.entryNodeId =
            entry.controlFlowGraph.entryNodeId;
        result.controlFlowGraph.exitNodeId =
            entry.controlFlowGraph.exitNodeId;
        result.semanticGraph.entryNodeId =
            entry.semanticGraph.entryNodeId;
        result.semanticGraph.exitNodeId =
            entry.semanticGraph.exitNodeId;

        requestInvocation(
            entryDocumentIndex,
            entry.semanticGraph.entryNodeId,
            entryContexts.constFirst(),
            0);
        scheduleDetachedComponents(
            entryDocumentIndex,
            0);
        processPendingFlowStates();
        materializeResolutionRecords();
        if (result.status ==
                StoryGraphProjectStatus::Complete &&
            (!result.controlFlowGraph.complete ||
             !result.semanticGraph.complete))
        {
            result.status =
                StoryGraphProjectStatus::Partial;
        }
        return result;
    }

private:
    bool validateRequest()
    {
        auto fail =
            [this](
                const QString& code,
                const QString& message,
                const StoryGraphSourceIdentity&
                    source =
                        StoryGraphSourceIdentity())
        {
            appendWarning(
                result,
                StoryGraphWarningSeverity::Error,
                code,
                message,
                source,
                StoryGraphSourceRange(),
                QString(),
                false);
            result.status =
                StoryGraphProjectStatus::Failed;
            return false;
        };

        if (request.budget.maximumSingleFileBytes <
                0 ||
            request.budget.maximumTotalBytes < 0 ||
            request.budget.maximumFileCount < 1 ||
            request.budget.maximumCallDepth < 0)
        {
            return fail(
                QStringLiteral(
                    "story_graph.project.invalid_budget"),
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "剧情图分析上限无效"));
        }

        QString rejectionReason;
        if (request.entrySource.identity.
                portableRootKey.isEmpty() ||
            !strictPath(
                request.entrySource.identity.virtualPath,
                &rejectionReason))
        {
            return fail(
                QStringLiteral(
                    "story_graph.project.entry_rejected"),
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "剧情图入口脚本身份或虚拟路径无效"),
                request.entrySource.identity);
        }

        QSet<QString> rootKeys;
        bool entryRootFound = false;
        bool entryRootActive = false;
        for (int index = 0;
             index <
             request.orderedContentRoots.size();
             ++index)
        {
            const StoryGraphContentRoot& root =
                request.orderedContentRoots.at(
                    index);
            if (root.portableRootKey.isEmpty() ||
                rootKeys.contains(
                    root.portableRootKey) ||
                root.ordinal != index)
            {
                return fail(
                    QStringLiteral(
                        "story_graph.project.roots_rejected"),
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "剧情图有序内容根身份、顺序或序号无效"));
            }
            rootKeys.insert(
                root.portableRootKey);
            if (root.portableRootKey ==
                request.entrySource.identity.
                    portableRootKey)
            {
                entryRootFound = true;
                entryRootActive =
                    root.kind ==
                    StoryGraphContentRootKind::
                        Active;
            }
        }
        if (!entryRootFound)
        {
            return fail(
                QStringLiteral(
                    "story_graph.project.entry_root_missing"),
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "剧情图入口脚本不属于给定的有序内容根"),
                request.entrySource.identity);
        }

        if (request.entryMapContext.state !=
                StoryGraphMapContextState::Unknown &&
            !strictPath(
                request.entryMapContext.
                    effectiveMapFolder,
                &rejectionReason))
        {
            return fail(
                QStringLiteral(
                    "story_graph.project.map_context_rejected"),
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "剧情图入口地图目录不是严格相对路径"),
                request.entrySource.identity);
        }
        if (request.entryMapContext.state ==
                StoryGraphMapContextState::Unknown &&
            !request.entryMapContext.
                effectiveMapFolder.isEmpty())
        {
            return fail(
                QStringLiteral(
                    "story_graph.project.map_context_rejected"),
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "未知地图上下文不得携带推测目录"),
                request.entrySource.identity);
        }

        for (const StoryGraphSourceSnapshot& snapshot :
             request.activeRootOpenSnapshots)
        {
            const QString& rootKey =
                snapshot.identity.portableRootKey;
            const auto rootIterator =
                std::find_if(
                    request.orderedContentRoots.cbegin(),
                    request.orderedContentRoots.cend(),
                    [&rootKey](
                        const StoryGraphContentRoot& root)
                    {
                        return root.portableRootKey ==
                            rootKey;
                    });
            if (rootIterator ==
                    request.orderedContentRoots.cend() ||
                rootIterator->kind !=
                    StoryGraphContentRootKind::Active ||
                !strictPath(
                    snapshot.identity.virtualPath,
                    &rejectionReason))
            {
                return fail(
                    QStringLiteral(
                        "story_graph.project.open_snapshot_rejected"),
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "打开脚本快照必须精确属于活动内容根"),
                    snapshot.identity);
            }
            const QString key =
                sourceKey(snapshot.identity);
            if (openSnapshots.contains(key))
            {
                return fail(
                    QStringLiteral(
                        "story_graph.project.open_snapshot_duplicate"),
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "同一活动根脚本存在重复打开快照"),
                    snapshot.identity);
            }
            StoryGraphSourceSnapshot prepared =
                snapshot;
            prepareSourceIdentity(prepared);
            openSnapshots.insert(
                key,
                std::move(prepared));
        }

        preparedEntrySource =
            request.entrySource;
        prepareSourceIdentity(
            preparedEntrySource);
        if (entryRootActive)
        {
            openSnapshots.insert(
                sourceKey(
                    preparedEntrySource.identity),
                preparedEntrySource);
        }
        return true;
    }

    void prepareSourceIdentity(
        StoryGraphSourceSnapshot& source) const
    {
        if (source.identity.contentSha256.isEmpty())
        {
            source.identity.contentSha256 =
                QCryptographicHash::hash(
                    source.utf8Bytes,
                    QCryptographicHash::Sha256);
        }
    }

    bool isCancelled() const
    {
        return cancelCallback &&
            cancelCallback();
    }

    void cancelProject()
    {
        result.status =
            StoryGraphProjectStatus::Cancelled;
        result.controlFlowGraph.complete = false;
        result.semanticGraph.complete = false;
        appendWarning(
            result,
            StoryGraphWarningSeverity::Information,
            QStringLiteral(
                "story_graph.project.cancelled"),
            QCoreApplication::translate(
                "StoryGraphProjectResolver",
                "剧情图项目分析已取消"),
            StoryGraphSourceIdentity(),
            StoryGraphSourceRange(),
            QString(),
            false);
    }

    bool checkDocumentBudget(
        const StoryGraphSourceSnapshot& source,
        bool entry)
    {
        const qsizetype byteCount =
            source.utf8Bytes.size();
        if (byteCount >
            request.budget.maximumSingleFileBytes)
        {
            appendWarning(
                result,
                StoryGraphWarningSeverity::Error,
                QStringLiteral(
                    "story_graph.project.budget.single_file"),
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "剧情图脚本超过单文件字节上限"),
                source.identity,
                StoryGraphSourceRange(),
                QString(),
                false);
            if (entry)
            {
                result.status =
                    StoryGraphProjectStatus::Failed;
            }
            return false;
        }
        if (result.totalBytesRead >
                request.budget.maximumTotalBytes -
                    byteCount)
        {
            appendWarning(
                result,
                StoryGraphWarningSeverity::Error,
                QStringLiteral(
                    "story_graph.project.budget.total_bytes"),
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "剧情图脚本总字节数超过上限"),
                source.identity,
                StoryGraphSourceRange(),
                QString(),
                false);
            if (entry)
            {
                result.status =
                    StoryGraphProjectStatus::Failed;
            }
            return false;
        }
        if (result.documents.size() >=
            request.budget.maximumFileCount)
        {
            appendWarning(
                result,
                StoryGraphWarningSeverity::Error,
                QStringLiteral(
                    "story_graph.project.budget.file_count"),
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "剧情图分析文件数超过上限"),
                source.identity,
                StoryGraphSourceRange(),
                QString(),
                false);
            if (entry)
            {
                result.status =
                    StoryGraphProjectStatus::Failed;
            }
            return false;
        }
        return true;
    }

    int ensureDocument(
        StoryGraphSourceSnapshot source,
        const QList<StoryGraphMapContext>& contexts)
    {
        const QString key =
            sourceKey(source.identity);
        const auto existing =
            documentIndexes.constFind(key);
        if (existing !=
            documentIndexes.cend())
        {
            return existing.value();
        }

        const bool entry =
            result.documents.isEmpty();
        if (!checkDocumentBudget(
                source,
                entry))
        {
            return -1;
        }
        if (isCancelled())
        {
            cancelProject();
            return -1;
        }

        StoryGraphAnalysisRequest
            analysisRequest;
        analysisRequest.source =
            std::move(source);
        analysisRequest.analysisGeneration =
            request.analysisGeneration;
        analysisRequest.includeUnknownCalls =
            request.includeUnknownCalls;
        if (contexts.size() == 1 &&
            contexts.first().state !=
                StoryGraphMapContextState::Unknown)
        {
            analysisRequest.effectiveMapFolder =
                contexts.first().
                    effectiveMapFolder;
            analysisRequest.effectiveMapFolderKnown =
                contexts.first().state ==
                StoryGraphMapContextState::Known;
        }

        result.totalBytesRead +=
            analysisRequest.source.
                utf8Bytes.size();
        StoryGraphDocumentResult document =
            StoryGraphAnalyzer::analyze(
                analysisRequest,
                cancelCallback);
        const int index =
            result.documents.size();
        documentIndexes.insert(key, index);
        result.documents.append(
            std::move(document));
        documentFlowIndexes.append(
            buildDocumentFlowIndex(
                result.documents.constLast()));

        const StoryGraphDocumentResult& stored =
            result.documents.constLast();
        appendGraph(
            result.controlFlowGraph,
            stored.controlFlowGraph,
            controlNodeIds,
            controlEdgeIds);
        appendGraph(
            result.semanticGraph,
            stored.semanticGraph,
            semanticNodeIds,
            semanticEdgeIds);
        for (const StoryGraphWarning& warning :
             stored.warnings)
        {
            result.warnings.append(warning);
        }

        if (stored.status ==
                StoryGraphDocumentStatus::Cancelled)
        {
            cancelProject();
        }
        else if (stored.status !=
                 StoryGraphDocumentStatus::Complete)
        {
            markPartial(result);
        }
        return index;
    }

    DocumentFlowIndex buildDocumentFlowIndex(
        const StoryGraphDocumentResult& document)
    {
        DocumentFlowIndex index;
        for (int nodeIndex = 0;
             nodeIndex <
                 document.semanticGraph.nodes.size();
             ++nodeIndex)
        {
            index.nodeIndexes.insert(
                document.semanticGraph.nodes.at(
                    nodeIndex).id,
                nodeIndex);
        }

        QHash<QString, QSet<QString>>
            incomingNodeIdsByScope;
        for (const StoryGraphEdge& edge :
             document.semanticGraph.edges)
        {
            const auto fromIterator =
                index.nodeIndexes.constFind(
                    edge.fromNodeId);
            const auto toIterator =
                index.nodeIndexes.constFind(
                    edge.toNodeId);
            if (fromIterator ==
                    index.nodeIndexes.cend() ||
                toIterator ==
                    index.nodeIndexes.cend())
            {
                continue;
            }
            const StoryGraphNode& fromNode =
                document.semanticGraph.nodes.at(
                    fromIterator.value());
            const StoryGraphNode& toNode =
                document.semanticGraph.nodes.at(
                    toIterator.value());
            if (fromNode.scopeId != toNode.scopeId)
                continue;
            QStringList& successors =
                index.successorNodeIds[
                    edge.fromNodeId];
            if (!successors.contains(
                    edge.toNodeId))
            {
                successors.append(
                    edge.toNodeId);
            }
            incomingNodeIdsByScope[
                toNode.scopeId].insert(
                    toNode.id);
        }

        QString chunkScopeId;
        const auto entryIterator =
            index.nodeIndexes.constFind(
                document.semanticGraph.entryNodeId);
        if (entryIterator !=
            index.nodeIndexes.cend())
        {
            chunkScopeId =
                document.semanticGraph.nodes.at(
                    entryIterator.value()).scopeId;
        }

        QSet<QString> scopesWithDetachedEntry;
        for (const StoryGraphNode& node :
             document.semanticGraph.nodes)
        {
            if (node.scopeId.isEmpty())
            {
                continue;
            }
            if (node.id ==
                    document.semanticGraph.
                        entryNodeId ||
                incomingNodeIdsByScope.value(
                    node.scopeId).contains(node.id))
            {
                continue;
            }
            index.detachedEntryNodeIds.append(
                node.id);
            scopesWithDetachedEntry.insert(
                node.scopeId);
        }
        QSet<QString> fallbackScopes;
        for (const StoryGraphNode& node :
             document.semanticGraph.nodes)
        {
            if (node.scopeId.isEmpty() ||
                node.scopeId == chunkScopeId ||
                scopesWithDetachedEntry.contains(
                    node.scopeId) ||
                fallbackScopes.contains(
                    node.scopeId))
            {
                continue;
            }
            fallbackScopes.insert(node.scopeId);
            index.detachedEntryNodeIds.append(
                node.id);
        }
        return index;
    }

    const StoryGraphNode* flowNode(
        int documentIndex,
        const QString& nodeId) const
    {
        if (documentIndex < 0 ||
            documentIndex >=
                documentFlowIndexes.size())
        {
            return nullptr;
        }
        const auto nodeIterator =
            documentFlowIndexes.at(
                documentIndex).
                nodeIndexes.constFind(nodeId);
        if (nodeIterator ==
            documentFlowIndexes.at(
                documentIndex).
                nodeIndexes.cend())
        {
            return nullptr;
        }
        return &result.documents.at(
            documentIndex).
            semanticGraph.nodes.at(
                nodeIterator.value());
    }

    QString invocationKey(
        int documentIndex,
        const QString& entryNodeId,
        const StoryGraphMapContext& context,
        int depth) const
    {
        return sourceKey(
                   result.documents.at(
                       documentIndex).source) +
            QChar::Null +
            entryNodeId +
            QChar::Null +
            contextKey(context) +
            QChar::Null +
            QString::number(depth);
    }

    QString requestInvocation(
        int documentIndex,
        const QString& entryNodeId,
        StoryGraphMapContext context,
        int depth)
    {
        if (documentIndex < 0 ||
            documentIndex >=
                result.documents.size())
        {
            return QString();
        }
        if (context.state ==
            StoryGraphMapContextState::Unknown)
        {
            context.effectiveMapFolder.clear();
        }
        const StoryGraphNode* entry =
            flowNode(
                documentIndex,
                entryNodeId);
        if (entry == nullptr)
            return QString();

        const QString key =
            invocationKey(
                documentIndex,
                entryNodeId,
                context,
                depth);
        if (!invocations.contains(key))
        {
            InvocationState invocation;
            invocation.documentIndex =
                documentIndex;
            invocation.entryNodeId =
                entryNodeId;
            invocation.scopeId =
                entry->scopeId;
            invocation.entryContext =
                context;
            invocation.depth = depth;
            invocations.insert(
                key,
                std::move(invocation));
            enqueueFlowState(
                key,
                entryNodeId,
                context);
        }
        return key;
    }

    void scheduleDetachedComponents(
        int documentIndex,
        int depth)
    {
        const QString scheduleKey =
            QString::number(documentIndex) +
            QChar::Null +
            QString::number(depth);
        if (scheduledDetachedComponents.contains(
                scheduleKey) ||
            documentIndex < 0 ||
            documentIndex >=
                documentFlowIndexes.size())
        {
            return;
        }
        scheduledDetachedComponents.insert(
            scheduleKey);
        StoryGraphMapContext unknown;
        unknown.state =
            StoryGraphMapContextState::Unknown;
        for (const QString& entryNodeId :
             documentFlowIndexes.at(
                 documentIndex).
                 detachedEntryNodeIds)
        {
            requestInvocation(
                documentIndex,
                entryNodeId,
                unknown,
                depth);
        }
    }

    void enqueueFlowState(
        const QString& invocationKeyValue,
        const QString& nodeId,
        StoryGraphMapContext context)
    {
        const auto invocationIterator =
            invocations.constFind(
                invocationKeyValue);
        if (invocationIterator ==
                invocations.cend() ||
            context.state !=
                    StoryGraphMapContextState::
                        Unknown &&
                context.effectiveMapFolder.isEmpty())
        {
            return;
        }
        if (context.state ==
            StoryGraphMapContextState::Unknown)
        {
            context.effectiveMapFolder.clear();
        }
        const StoryGraphNode* node =
            flowNode(
                invocationIterator->
                    documentIndex,
                nodeId);
        if (node == nullptr ||
            node->scopeId !=
                invocationIterator->scopeId)
        {
            return;
        }

        const QString bucketKey =
            invocationKeyValue +
            QChar::Null +
            nodeId;
        QSet<QString>& accepted =
            acceptedFlowContexts[bucketKey];
        if (collapsedFlowBuckets.contains(
                bucketKey))
        {
            return;
        }
        QString key = contextKey(context);
        if (accepted.contains(key))
            return;
        if (accepted.size() >=
            MaximumTrackedMapContexts)
        {
            context.state =
                StoryGraphMapContextState::Unknown;
            context.effectiveMapFolder.clear();
            key = contextKey(context);
            accepted.clear();
            accepted.insert(key);
            collapsedFlowBuckets.insert(
                bucketKey);
            for (qsizetype index =
                     pendingFlowStates.size();
                 index >
                     nextPendingFlowStateIndex;)
            {
                --index;
                const PendingFlowState& pending =
                    pendingFlowStates.at(index);
                if (pending.invocationKey ==
                        invocationKeyValue &&
                    pending.nodeId == nodeId)
                {
                    pendingFlowStates.removeAt(
                        index);
                }
            }
            const QString warningKey =
                QStringLiteral(
                    "map-contexts|") +
                bucketKey;
            if (!flowWarningKeys.contains(
                    warningKey))
            {
                flowWarningKeys.insert(
                    warningKey);
                appendWarning(
                    result,
                    StoryGraphWarningSeverity::
                        Warning,
                    QStringLiteral(
                        "story_graph.project.budget.map_contexts"),
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "地图上下文分支过多，已折叠为未知上下文"),
                    node->source,
                        node->sourceRange,
                        node->id);
            }
            pendingFlowStates.append({
                invocationKeyValue,
                nodeId,
                context});
            return;
        }
        accepted.insert(key);
        pendingFlowStates.append({
            invocationKeyValue,
            nodeId,
            context});
    }

    QStringList flowSuccessors(
        const QString& invocationKeyValue,
        const QString& nodeId) const
    {
        const auto invocationIterator =
            invocations.constFind(
                invocationKeyValue);
        if (invocationIterator ==
            invocations.cend())
        {
            return {};
        }
        return documentFlowIndexes.at(
                   invocationIterator->
                       documentIndex).
            successorNodeIds.value(nodeId);
    }

    void propagateToSuccessors(
        const QString& invocationKeyValue,
        const QStringList& successors,
        const QList<StoryGraphMapContext>&
            contexts)
    {
        for (const QString& successor :
             successors)
        {
            for (const StoryGraphMapContext& context :
                 contexts)
            {
                enqueueFlowState(
                    invocationKeyValue,
                    successor,
                    context);
            }
        }
    }

    void propagateOpaqueCallContinuation(
        const QString& callerInvocationKey,
        const StoryGraphNode& caller,
        const QStringList& successors,
        const StoryGraphMapContext& context)
    {
        if (caller.kind ==
            StoryGraphNodeKind::ParallelScriptCall)
        {
            propagateToSuccessors(
                callerInvocationKey,
                successors,
                {context});
            return;
        }

        StoryGraphMapContext unknown;
        unknown.state =
            StoryGraphMapContextState::Unknown;
        propagateToSuccessors(
            callerInvocationKey,
            successors,
            {unknown});
    }

    void publishInvocationExit(
        const QString& invocationKeyValue,
        StoryGraphMapContext context)
    {
        if (context.state ==
            StoryGraphMapContextState::Unknown)
        {
            context.effectiveMapFolder.clear();
        }
        auto invocationIterator =
            invocations.find(
                invocationKeyValue);
        if (invocationIterator ==
            invocations.end())
        {
            return;
        }
        const QString key = contextKey(context);
        if (invocationIterator->
                exitContextKeys.contains(key))
        {
            return;
        }
        invocationIterator->
            exitContextKeys.insert(key);
        invocationIterator->
            exitContexts.append(context);
        const QList<FlowContinuation>
            continuations =
                invocationIterator->
                    continuations;
        for (const FlowContinuation& continuation :
             continuations)
        {
            propagateToSuccessors(
                continuation.
                    callerInvocationKey,
                continuation.successorNodeIds,
                {context});
        }
    }

    void registerContinuation(
        const QString& targetInvocationKey,
        const QString& callerInvocationKey,
        const StoryGraphNode& caller,
        const QStringList& successors)
    {
        auto targetIterator =
            invocations.find(
                targetInvocationKey);
        if (targetIterator ==
            invocations.end())
        {
            return;
        }
        const QString key =
            callerInvocationKey +
            QChar::Null +
            caller.id;
        if (!targetIterator->
                continuationKeys.contains(key))
        {
            targetIterator->
                continuationKeys.insert(key);
            FlowContinuation continuation;
            continuation.callerInvocationKey =
                callerInvocationKey;
            continuation.successorNodeIds =
                successors;
            targetIterator->
                continuations.append(
                    std::move(continuation));
        }
        const QList<StoryGraphMapContext>
            knownExitContexts =
                targetIterator->exitContexts;
        for (const StoryGraphMapContext& context :
             knownExitContexts)
        {
            propagateToSuccessors(
                callerInvocationKey,
                successors,
                {context});
        }
    }

    void processPendingFlowStates()
    {
        while (nextPendingFlowStateIndex <
                   pendingFlowStates.size() &&
               result.status !=
                   StoryGraphProjectStatus::Cancelled)
        {
            if (isCancelled())
            {
                cancelProject();
                return;
            }
            const PendingFlowState state =
                pendingFlowStates.at(
                    nextPendingFlowStateIndex++);
            const auto invocationIterator =
                invocations.constFind(
                    state.invocationKey);
            if (invocationIterator ==
                invocations.cend())
            {
                continue;
            }
            const StoryGraphNode* node =
                flowNode(
                    invocationIterator->
                        documentIndex,
                    state.nodeId);
            if (node == nullptr)
                continue;

            const StoryGraphDocumentResult&
                document =
                    result.documents.at(
                        invocationIterator->
                            documentIndex);
            if (state.nodeId ==
                document.semanticGraph.exitNodeId)
            {
                publishInvocationExit(
                    state.invocationKey,
                    state.context);
                continue;
            }

            const QStringList successors =
                flowSuccessors(
                    state.invocationKey,
                    state.nodeId);
            if (node->kind ==
                StoryGraphNodeKind::MapLoad)
            {
                propagateToSuccessors(
                    state.invocationKey,
                    successors,
                    contextsAfterMapLoad(
                        {state.context},
                        *node));
                continue;
            }
            if (node->apiName ==
                    QStringLiteral("loadgame") &&
                node->kind ==
                    StoryGraphNodeKind::
                        RegisteredApiCall &&
                (node->certainty ==
                     StoryGraphCertainty::Certain ||
                 node->certainty ==
                     StoryGraphCertainty::Dynamic))
            {
                StoryGraphMapContext unknown;
                unknown.state =
                    StoryGraphMapContextState::
                        Unknown;
                propagateToSuccessors(
                    state.invocationKey,
                    successors,
                    {unknown});
                continue;
            }
            if (node->kind ==
                    StoryGraphNodeKind::
                        SerialScriptCall ||
                node->kind ==
                    StoryGraphNodeKind::
                        ParallelScriptCall)
            {
                resolveFlowCall(
                    state.invocationKey,
                    *node,
                    state.context,
                    successors);
                continue;
            }
            propagateToSuccessors(
                state.invocationKey,
                successors,
                {state.context});
        }
    }

    void appendFlowWarningOnce(
        const QString& diagnosticCode,
        const QString& message,
        const StoryGraphNode& node)
    {
        const QString key =
            diagnosticCode +
            QChar::Null +
            node.id;
        if (flowWarningKeys.contains(key))
            return;
        flowWarningKeys.insert(key);
        appendWarning(
            result,
            StoryGraphWarningSeverity::Warning,
            diagnosticCode,
            message,
            node.source,
            node.sourceRange,
            node.id);
    }

    QList<StoryGraphMapContext>
    contextsAfterMapLoad(
        const QList<StoryGraphMapContext>&
            currentContexts,
        const StoryGraphNode& node)
    {
        QList<StoryGraphMapContext> branches;
        for (StoryGraphMapContext context :
             currentContexts)
        {
            if (context.state ==
                StoryGraphMapContextState::Known)
            {
                context.state =
                    StoryGraphMapContextState::
                        Assumed;
            }
            branches.append(
                std::move(context));
        }

        QString rejectionReason;
        if (node.certainty !=
            StoryGraphCertainty::Certain)
        {
            StoryGraphMapContext unknown;
            unknown.state =
                StoryGraphMapContextState::Unknown;
            branches.append(unknown);
            bool collapsed = false;
            return normalizeContexts(
                branches,
                &collapsed);
        }
        if (!strictPath(
                node.literalTarget,
                &rejectionReason))
        {
            appendFlowWarningOnce(
                QStringLiteral(
                    "story_graph.project.map_target_rejected"),
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "loadmap 字面量目标不是严格相对路径") +
                    QStringLiteral(": ") +
                    rejectionReason,
                node);
            StoryGraphMapContext unknown;
            unknown.state =
                StoryGraphMapContextState::Unknown;
            branches.append(unknown);
            bool collapsed = false;
            return normalizeContexts(
                branches,
                &collapsed);
        }

        QString newFolder =
            mapBasename(
                node.literalTarget);
        StoryGraphMapFolderResolution
            folderResolution;
        if (mapFolderResolver)
        {
            try
            {
                folderResolution =
                    mapFolderResolver(
                        node.literalTarget);
            }
            catch (const std::exception& error)
            {
                folderResolution.status =
                    StoryGraphMapFolderResolutionStatus::
                        Error;
                folderResolution.message =
                    QString::fromUtf8(error.what());
            }
            catch (...)
            {
                folderResolution.status =
                    StoryGraphMapFolderResolutionStatus::
                        Error;
                folderResolution.message =
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "地图目录解析回调抛出未知异常");
            }
            if (folderResolution.status ==
                StoryGraphMapFolderResolutionStatus::
                    Resolved)
            {
                QString folderRejection;
                if (strictPath(
                        folderResolution.
                            effectiveMapFolder,
                        &folderRejection))
                {
                    newFolder =
                        folderResolution.
                            effectiveMapFolder;
                }
                else
                {
                    folderResolution.status =
                        StoryGraphMapFolderResolutionStatus::
                            Rejected;
                    folderResolution.message =
                        folderRejection;
                }
            }
            if (folderResolution.status ==
                    StoryGraphMapFolderResolutionStatus::
                        Rejected ||
                folderResolution.status ==
                    StoryGraphMapFolderResolutionStatus::
                        Error ||
                folderResolution.status ==
                    StoryGraphMapFolderResolutionStatus::
                        ContextDependent)
            {
                appendFlowWarningOnce(
                    QStringLiteral(
                        "story_graph.project.map_folder_context_dependent"),
                    folderResolution.message.isEmpty()
                        ? QCoreApplication::translate(
                            "StoryGraphProjectResolver",
                            "地图目录映射无法静态确定，保留 basename 分支")
                        : folderResolution.message,
                    node);
            }
        }

        StoryGraphMapContext next;
        next.state =
            StoryGraphMapContextState::Assumed;
        next.effectiveMapFolder =
            newFolder;
        branches.append(std::move(next));
        bool collapsed = false;
        branches =
            normalizeContexts(
                branches,
                &collapsed);
        if (collapsed)
        {
            appendFlowWarningOnce(
                QStringLiteral(
                    "story_graph.project.budget.map_contexts"),
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "地图上下文分支过多，已折叠为未知上下文"),
                node);
        }
        return branches;
    }

    void resolveFlowCall(
        const QString& callerInvocationKey,
        const StoryGraphNode& caller,
        const StoryGraphMapContext& context,
        const QStringList& successors)
    {
        const auto invocationIterator =
            invocations.constFind(
                callerInvocationKey);
        if (invocationIterator ==
            invocations.cend())
        {
            return;
        }
        const int depth =
            invocationIterator->depth;
        const StoryGraphEdgeKind edgeKind =
            caller.kind ==
                    StoryGraphNodeKind::
                        ParallelScriptCall
            ? StoryGraphEdgeKind::ParallelCall
            : StoryGraphEdgeKind::Call;

        QString rejectionReason;
        if (caller.literalTarget.isEmpty())
        {
            BranchResolution branch;
            branch.status =
                StoryGraphTargetResolutionStatus::
                    ContextDependent;
            branch.context = context;
            branch.contextDependent = true;
            branch.message =
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "脚本目标不是可确认的字面量");
            storeBranchResolution(
                caller,
                branch,
                edgeKind,
                QString(),
                depth);
            propagateOpaqueCallContinuation(
                callerInvocationKey,
                caller,
                successors,
                context);
            return;
        }
        if (!strictPath(
                caller.literalTarget,
                &rejectionReason))
        {
            BranchResolution branch;
            branch.status =
                StoryGraphTargetResolutionStatus::
                    Rejected;
            branch.context = context;
            branch.message =
                rejectionReason;
            storeBranchResolution(
                caller,
                branch,
                edgeKind,
                QString(),
                depth);
            propagateOpaqueCallContinuation(
                callerInvocationKey,
                caller,
                successors,
                context);
            return;
        }
        if (caller.certainty !=
            StoryGraphCertainty::Certain)
        {
            BranchResolution branch;
            branch.status =
                StoryGraphTargetResolutionStatus::
                    ContextDependent;
            branch.context = context;
            branch.contextDependent = true;
            branch.message =
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "脚本调用不是可确认的直接全局调用");
            storeBranchResolution(
                caller,
                branch,
                edgeKind,
                QString(),
                depth);
            propagateOpaqueCallContinuation(
                callerInvocationKey,
                caller,
                successors,
                context);
            return;
        }
        if (depth >=
            request.budget.maximumCallDepth)
        {
            BranchResolution branch;
            branch.status =
                StoryGraphTargetResolutionStatus::
                    BudgetExceeded;
            branch.context = context;
            branch.message =
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "剧情图跨文件调用深度超过上限");
            storeBranchResolution(
                caller,
                branch,
                edgeKind,
                QString(),
                depth);
            // The depth boundary is an analysis boundary, not a runtime
            // assertion that the caller cannot continue. A serial target can
            // change the effective map before returning, so its continuation
            // must be conservative until a later shorter invocation can
            // supersede the deferred budget relation.
            propagateOpaqueCallContinuation(
                callerInvocationKey,
                caller,
                successors,
                context);
            return;
        }

        BranchResolution branch =
            resolveBranch(
                caller.literalTarget,
                context);
        branch.contextDependent =
            context.state !=
                StoryGraphMapContextState::Known;
        if (branch.contextDependent &&
            branch.status ==
                StoryGraphTargetResolutionStatus::
                    Resolved)
        {
            branch.status =
                StoryGraphTargetResolutionStatus::
                    ContextDependent;
        }
        if (branch.status ==
            StoryGraphTargetResolutionStatus::Cancelled)
        {
            storeBranchResolution(
                caller,
                branch,
                edgeKind,
                QString(),
                depth);
            cancelProject();
            return;
        }
        if (branch.source.identity.
                portableRootKey.isEmpty())
        {
            storeBranchResolution(
                caller,
                branch,
                edgeKind,
                QString(),
                depth);
            propagateOpaqueCallContinuation(
                callerInvocationKey,
                caller,
                successors,
                context);
            return;
        }

        const int targetDocumentIndex =
            ensureDocument(
                branch.source,
                {context});
        if (targetDocumentIndex < 0)
        {
            if (result.status !=
                StoryGraphProjectStatus::Cancelled)
            {
                BranchResolution budget =
                    branch;
                budget.status =
                    StoryGraphTargetResolutionStatus::
                        BudgetExceeded;
                budget.source =
                    StoryGraphSourceSnapshot();
                budget.message =
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "剧情图目标超过文件数或字节预算");
                storeBranchResolution(
                    caller,
                    budget,
                    edgeKind,
                    QString(),
                    depth);
                propagateOpaqueCallContinuation(
                    callerInvocationKey,
                    caller,
                    successors,
                    context);
            }
            return;
        }

        const StoryGraphDocumentResult& target =
            result.documents.at(
                targetDocumentIndex);
        if (target.status ==
                StoryGraphDocumentStatus::Failed ||
            target.semanticGraph.entryNodeId.isEmpty())
        {
            BranchResolution unavailable =
                branch;
            unavailable.status =
                StoryGraphTargetResolutionStatus::
                    ReadError;
            unavailable.message =
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "目标脚本无法形成可用的剧情语义入口");
            storeBranchResolution(
                caller,
                unavailable,
                edgeKind,
                QString(),
                depth);
            propagateOpaqueCallContinuation(
                callerInvocationKey,
                caller,
                successors,
                context);
            return;
        }
        const QString targetInvocationKey =
            requestInvocation(
                targetDocumentIndex,
                target.semanticGraph.entryNodeId,
                context,
                depth + 1);
        if (targetInvocationKey.isEmpty())
        {
            BranchResolution unavailable =
                branch;
            unavailable.status =
                StoryGraphTargetResolutionStatus::
                    ReadError;
            unavailable.message =
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "目标脚本剧情语义入口不可遍历");
            storeBranchResolution(
                caller,
                unavailable,
                edgeKind,
                QString(),
                depth);
            propagateOpaqueCallContinuation(
                callerInvocationKey,
                caller,
                successors,
                context);
            return;
        }
        storeBranchResolution(
            caller,
            branch,
            edgeKind,
            target.semanticGraph.entryNodeId,
            depth);
        scheduleDetachedComponents(
            targetDocumentIndex,
            depth + 1);
        if (caller.kind ==
            StoryGraphNodeKind::
                ParallelScriptCall)
        {
            propagateToSuccessors(
                callerInvocationKey,
                successors,
                {context});
            return;
        }
        registerContinuation(
            targetInvocationKey,
            callerInvocationKey,
            caller,
            successors);
    }

    BranchResolution resolveBranch(
        const QString& literalTarget,
        const StoryGraphMapContext& context)
    {
        BranchResolution resolution;
        resolution.context = context;
        resolution.contextDependent =
            context.state !=
                StoryGraphMapContextState::Known;

        QStringList candidates;
        if (context.state !=
            StoryGraphMapContextState::Unknown)
        {
            candidates.append(
                QStringLiteral("script/map/") +
                context.effectiveMapFolder +
                QLatin1Char('/') +
                literalTarget);
        }
        candidates.append(
            QStringLiteral("script/goods/") +
            literalTarget);
        candidates.append(
            QStringLiteral("script/common/") +
            literalTarget);

        for (const QString& candidate :
             candidates)
        {
            QString rejectionReason;
            if (!strictPath(
                    candidate,
                    &rejectionReason))
            {
                resolution.status =
                    StoryGraphTargetResolutionStatus::
                        Rejected;
                resolution.candidateVirtualPath =
                    candidate;
                resolution.message =
                    rejectionReason;
                return resolution;
            }

            bool zeroByteHit = false;
            for (const StoryGraphContentRoot& root :
                 request.orderedContentRoots)
            {
                if (isCancelled())
                {
                    resolution.status =
                        StoryGraphTargetResolutionStatus::
                            Cancelled;
                    resolution.candidateVirtualPath =
                        candidate;
                    resolution.message =
                        QCoreApplication::translate(
                            "StoryGraphProjectResolver",
                            "剧情图项目分析已取消");
                    return resolution;
                }

                const CachedRead cached =
                    readSource(
                        root,
                        candidate);
                if (cached.budgetExceeded)
                {
                    resolution.status =
                        StoryGraphTargetResolutionStatus::
                            BudgetExceeded;
                    resolution.candidateVirtualPath =
                        candidate;
                    resolution.message =
                        cached.budgetMessage;
                    resolution.diagnosticCode =
                        cached.budgetDiagnosticCode;
                    return resolution;
                }
                switch (cached.result.status)
                {
                case StoryGraphReadStatus::Missing:
                    continue;
                case StoryGraphReadStatus::Found:
                    if (cached.result.
                            utf8Bytes.isEmpty())
                    {
                        zeroByteHit = true;
                        break;
                    }
                    resolution.status =
                        StoryGraphTargetResolutionStatus::
                            Resolved;
                    resolution.candidateVirtualPath =
                        candidate;
                    resolution.source.identity.
                        portableRootKey =
                            root.portableRootKey;
                    resolution.source.identity.
                        virtualPath =
                            candidate;
                    resolution.source.identity.
                        canonicalAbsolutePath =
                            cached.result.
                                canonicalAbsolutePath;
                    resolution.source.identity.
                        contentSha256 =
                            cached.contentSha256;
                    resolution.source.identity.
                        documentRevision =
                            cached.documentRevision;
                    resolution.source.identity.
                        fromEditorBuffer =
                            cached.fromEditorBuffer;
                    resolution.source.utf8Bytes =
                        cached.result.utf8Bytes;
                    return resolution;
                case StoryGraphReadStatus::Rejected:
                    resolution.status =
                        StoryGraphTargetResolutionStatus::
                            Rejected;
                    resolution.candidateVirtualPath =
                        candidate;
                    resolution.message =
                        cached.result.message;
                    return resolution;
                case StoryGraphReadStatus::Error:
                    resolution.status =
                        StoryGraphTargetResolutionStatus::
                            ReadError;
                    resolution.candidateVirtualPath =
                        candidate;
                    resolution.message =
                        cached.result.message;
                    return resolution;
                case StoryGraphReadStatus::Cancelled:
                    resolution.status =
                        StoryGraphTargetResolutionStatus::
                            Cancelled;
                    resolution.candidateVirtualPath =
                        candidate;
                    resolution.message =
                        cached.result.message;
                    return resolution;
                }
                if (zeroByteHit)
                    break;
            }
            if (zeroByteHit)
            {
                // Runtime lookup treats this candidate as hit but unusable.
                // Continue with the next candidate category, never with a
                // later root for the same candidate.
                continue;
            }
        }

        resolution.status =
            resolution.contextDependent
            ? StoryGraphTargetResolutionStatus::
                ContextDependent
            : StoryGraphTargetResolutionStatus::
                Missing;
        resolution.candidateVirtualPath =
            candidates.isEmpty()
            ? QString()
            : candidates.constLast();
        resolution.message =
            resolution.contextDependent
            ? QCoreApplication::translate(
                "StoryGraphProjectResolver",
                "地图上下文不确定，脚本目标只能标记为潜在关系")
            : QCoreApplication::translate(
                "StoryGraphProjectResolver",
                "有序内容根中未找到脚本目标");
        return resolution;
    }

    CachedRead readSource(
        const StoryGraphContentRoot& root,
        const QString& virtualPath)
    {
        const QString key =
            readKey(root, virtualPath);
        const auto cached =
            readCache.constFind(key);
        if (cached != readCache.cend())
            return cached.value();

        CachedRead value;
        const bool existingDocument =
            documentIndexes.contains(key);
        if (!existingDocument &&
            result.documents.size() >=
                request.budget.maximumFileCount)
        {
            value.budgetExceeded = true;
            value.budgetDiagnosticCode =
                QStringLiteral(
                    "story_graph.project.budget.file_count");
            value.budgetMessage =
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "剧情图分析文件数超过上限");
            return value;
        }

        const auto snapshot =
            openSnapshots.constFind(key);
        if (root.kind ==
                StoryGraphContentRootKind::Active &&
            snapshot != openSnapshots.cend())
        {
            value.result.status =
                StoryGraphReadStatus::Found;
            value.result.utf8Bytes =
                snapshot->utf8Bytes;
            value.result.canonicalAbsolutePath =
                snapshot->identity.
                    canonicalAbsolutePath;
            value.fromEditorBuffer = true;
            value.documentRevision =
                snapshot->identity.
                    documentRevision;
            value.contentSha256 =
                snapshot->identity.
                    contentSha256;
        }
        else if (!readCallback)
        {
            value.result.status =
                StoryGraphReadStatus::Error;
            value.result.message =
                QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "未提供安全资源读取回调");
        }
        else
        {
            try
            {
                value.result =
                    readCallback(
                        root,
                        virtualPath);
            }
            catch (const std::exception& error)
            {
                value.result.status =
                    StoryGraphReadStatus::Error;
                value.result.message =
                    QString::fromUtf8(error.what());
            }
            catch (...)
            {
                value.result.status =
                    StoryGraphReadStatus::Error;
                value.result.message =
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "安全资源读取回调抛出未知异常");
            }
        }

        if (value.result.status ==
                StoryGraphReadStatus::Found &&
            !existingDocument)
        {
            const qsizetype byteCount =
                value.result.utf8Bytes.size();
            if (byteCount >
                request.budget.maximumSingleFileBytes)
            {
                value.budgetExceeded = true;
                value.budgetDiagnosticCode =
                    QStringLiteral(
                        "story_graph.project.budget.single_file");
                value.budgetMessage =
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "剧情图脚本超过单文件字节上限");
            }
            else if (
                byteCount >
                    request.budget.maximumTotalBytes ||
                result.totalBytesRead >
                    request.budget.maximumTotalBytes -
                        byteCount)
            {
                value.budgetExceeded = true;
                value.budgetDiagnosticCode =
                    QStringLiteral(
                        "story_graph.project.budget.total_bytes");
                value.budgetMessage =
                    QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "剧情图脚本总字节数超过上限");
            }
            if (value.budgetExceeded)
            {
                value.result.utf8Bytes.clear();
                value.result.utf8Bytes.squeeze();
                value.contentSha256.clear();
                return value;
            }
        }

        if (value.result.status ==
                StoryGraphReadStatus::Found &&
            value.contentSha256.isEmpty())
        {
            value.contentSha256 =
                QCryptographicHash::hash(
                    value.result.utf8Bytes,
                    QCryptographicHash::Sha256);
        }
        readCache.insert(key, value);
        return value;
    }

    QString branchResolutionKey(
        const StoryGraphNode& caller,
        const StoryGraphMapContext& context,
        StoryGraphEdgeKind edgeKind) const
    {
        return caller.id +
            QChar::Null +
            contextKey(context) +
            QChar::Null +
            QString::number(
                static_cast<int>(edgeKind));
    }

    void storeBranchResolution(
        const StoryGraphNode& caller,
        const BranchResolution& branch,
        StoryGraphEdgeKind edgeKind,
        const QString& targetEntryNodeId,
        int depth)
    {
        const QString key =
            branchResolutionKey(
                caller,
                branch.context,
                edgeKind);
        const auto existing =
            pendingResolutionRecords.constFind(
                key);
        if (existing !=
            pendingResolutionRecords.cend())
        {
            const bool existingBudget =
                existing->branch.status ==
                    StoryGraphTargetResolutionStatus::
                        BudgetExceeded;
            const bool replacementBudget =
                branch.status ==
                    StoryGraphTargetResolutionStatus::
                        BudgetExceeded;
            if ((!existingBudget &&
                 replacementBudget) ||
                depth > existing->depth)
            {
                return;
            }
        }
        else
        {
            pendingResolutionOrder.append(key);
        }

        PendingResolutionRecord record;
        record.caller = caller;
        record.branch = branch;
        record.edgeKind = edgeKind;
        record.targetEntryNodeId =
            targetEntryNodeId;
        record.depth = depth;
        pendingResolutionRecords.insert(
            key,
            std::move(record));
    }

    void materializeResolutionRecords()
    {
        for (const QString& key :
             pendingResolutionOrder)
        {
            const auto recordIterator =
                pendingResolutionRecords.constFind(
                    key);
            if (recordIterator ==
                pendingResolutionRecords.cend())
            {
                continue;
            }
            const PendingResolutionRecord& record =
                recordIterator.value();
            appendBranchResolution(
                record.caller,
                record.branch,
                record.edgeKind);
            if (!record.targetEntryNodeId.isEmpty() &&
                !record.branch.source.identity.
                    portableRootKey.isEmpty())
            {
                addCrossFileEdge(
                    record.caller,
                    record.targetEntryNodeId,
                    record.edgeKind,
                    record.branch.contextDependent
                        ? StoryGraphCertainty::Unknown
                        : StoryGraphCertainty::Certain,
                    record.branch);
            }
        }
    }

    void appendBranchResolution(
        const StoryGraphNode& caller,
        const BranchResolution& branch,
        StoryGraphEdgeKind edgeKind)
    {
        StoryGraphTargetResolution resolution;
        resolution.callerNodeId =
            caller.id;
        resolution.callerSource =
            caller.source;
        resolution.callerRange =
            caller.sourceRange;
        resolution.literalTarget =
            caller.literalTarget;
        resolution.candidateVirtualPath =
            branch.candidateVirtualPath;
        resolution.mapContext =
            branch.context;
        resolution.status =
            branch.status;
        resolution.targetPortableRootKey =
            branch.source.identity.
                portableRootKey;
        resolution.targetVirtualPath =
            branch.source.identity.
                virtualPath;
        resolution.edgeKind =
            edgeKind;
        resolution.message =
            branch.message;
        result.targetResolutions.append(
            resolution);

        if (branch.status ==
                StoryGraphTargetResolutionStatus::
                    Resolved)
        {
            return;
        }
        if (branch.status ==
                StoryGraphTargetResolutionStatus::
                    Cancelled)
        {
            addResolutionPlaceholder(
                caller,
                branch,
                edgeKind);
            cancelProject();
            return;
        }
        if (branch.status ==
                StoryGraphTargetResolutionStatus::
                    ContextDependent &&
            !branch.source.identity.
                portableRootKey.isEmpty())
        {
            addResolutionPlaceholder(
                caller,
                branch,
                edgeKind);
            appendWarning(
                result,
                StoryGraphWarningSeverity::Warning,
                QStringLiteral(
                    "story_graph.project.context_dependent"),
                branch.message.isEmpty()
                    ? QCoreApplication::translate(
                        "StoryGraphProjectResolver",
                        "脚本跨文件关系依赖运行时地图上下文")
                    : branch.message,
                caller.source,
                caller.sourceRange,
                caller.id);
            return;
        }

        addResolutionPlaceholder(
            caller,
            branch,
            edgeKind);
        QString diagnosticCode;
        switch (branch.status)
        {
        case StoryGraphTargetResolutionStatus::Missing:
            diagnosticCode =
                QStringLiteral(
                    "story_graph.project.target_missing");
            break;
        case StoryGraphTargetResolutionStatus::Rejected:
            diagnosticCode =
                QStringLiteral(
                    "story_graph.project.target_rejected");
            break;
        case StoryGraphTargetResolutionStatus::ReadError:
            diagnosticCode =
                QStringLiteral(
                    "story_graph.project.read_error");
            break;
        case StoryGraphTargetResolutionStatus::
                ContextDependent:
            diagnosticCode =
                QStringLiteral(
                    "story_graph.project.context_dependent");
            break;
        case StoryGraphTargetResolutionStatus::
                BudgetExceeded:
            diagnosticCode =
                branch.diagnosticCode.isEmpty()
                ? QStringLiteral(
                      "story_graph.project.budget.call")
                : branch.diagnosticCode;
            break;
        case StoryGraphTargetResolutionStatus::Cancelled:
            diagnosticCode =
                QStringLiteral(
                    "story_graph.project.cancelled");
            break;
        case StoryGraphTargetResolutionStatus::Resolved:
            break;
        }
        appendWarning(
            result,
            StoryGraphWarningSeverity::Warning,
            diagnosticCode,
            branch.message.isEmpty()
                ? QCoreApplication::translate(
                    "StoryGraphProjectResolver",
                    "脚本跨文件目标无法确定")
                : branch.message,
            caller.source,
            caller.sourceRange,
            caller.id);
    }

    QString addResolutionPlaceholder(
        const StoryGraphNode& caller,
        const BranchResolution& branch,
        StoryGraphEdgeKind edgeKind)
    {
        const bool missingLike =
            branch.status ==
                StoryGraphTargetResolutionStatus::
                    Missing ||
            (branch.status ==
                 StoryGraphTargetResolutionStatus::
                     ContextDependent &&
             branch.source.identity.
                 portableRootKey.isEmpty());
        const StoryGraphNodeKind kind =
            missingLike
            ? StoryGraphNodeKind::MissingTarget
            : StoryGraphNodeKind::Warning;
        const QString fingerprint =
            resolutionFingerprint(
                branch.status,
                caller,
                caller.literalTarget,
                branch.context,
                branch.candidateVirtualPath,
                branch.source.identity.
                    portableRootKey,
                branch.source.identity.virtualPath);

        StoryGraphStableNodeIdInput idInput;
        idInput.portableRootKey =
            caller.source.portableRootKey;
        idInput.strictVirtualPath =
            caller.source.virtualPath;
        idInput.scopeId =
            caller.scopeId;
        idInput.kind = kind;
        idInput.semanticFingerprint =
            fingerprint;
        idInput.structuralOccurrenceKey =
            QStringLiteral("call-site:") +
            caller.id;

        StoryGraphNode placeholder;
        placeholder.id =
            makeStoryGraphNodeId(idInput);
        placeholder.kind = kind;
        placeholder.certainty =
            branch.contextDependent ||
                    branch.status ==
                        StoryGraphTargetResolutionStatus::
                            ContextDependent
            ? StoryGraphCertainty::Unknown
            : StoryGraphCertainty::Warning;
        placeholder.title =
            missingLike
            ? QCoreApplication::translate(
                "StoryGraphProjectResolver",
                "未解析脚本目标")
            : QCoreApplication::translate(
                "StoryGraphProjectResolver",
                "脚本解析警告");
        placeholder.summary =
            branch.message;
        placeholder.source =
            caller.source;
        placeholder.sourceRange =
            caller.sourceRange;
        placeholder.scopeId =
            caller.scopeId;
        placeholder.literalTarget =
            caller.literalTarget;
        placeholder.resolvedPortableRootKey =
            branch.source.identity.
                portableRootKey;
        placeholder.resolvedVirtualPath =
            branch.source.identity.
                virtualPath;
        placeholder.semanticFingerprint =
            fingerprint;
        placeholder.structuralOccurrenceKey =
            idInput.structuralOccurrenceKey;
        if (!semanticNodeIds.contains(
                placeholder.id))
        {
            semanticNodeIds.insert(
                placeholder.id);
            result.semanticGraph.nodes.append(
                placeholder);
        }
        addCrossFileEdge(
            caller,
            placeholder.id,
            edgeKind,
            placeholder.certainty,
            branch);
        return placeholder.id;
    }

    void addCrossFileEdge(
        const StoryGraphNode& caller,
        const QString& targetNodeId,
        StoryGraphEdgeKind edgeKind,
        StoryGraphCertainty certainty,
        const BranchResolution& branch)
    {
        if (targetNodeId.isEmpty())
            return;
        const QString fingerprint =
            resolutionFingerprint(
                branch.status,
                caller,
                caller.literalTarget,
                branch.context,
                branch.candidateVirtualPath,
                branch.source.identity.
                    portableRootKey,
                branch.source.identity.virtualPath);
        StoryGraphStableEdgeIdInput idInput;
        idInput.fromNodeId =
            caller.id;
        idInput.toNodeId =
            targetNodeId;
        idInput.kind = edgeKind;
        idInput.semanticFingerprint =
            fingerprint;
        idInput.structuralOccurrenceKey =
            QStringLiteral("project-resolution");

        StoryGraphEdge edge;
        edge.id =
            makeStoryGraphEdgeId(idInput);
        edge.fromNodeId =
            caller.id;
        edge.toNodeId =
            targetNodeId;
        edge.kind = edgeKind;
        edge.certainty = certainty;
        edge.label =
            storyGraphTargetResolutionStatusToString(
                branch.status);
        edge.semanticFingerprint =
            fingerprint;
        edge.structuralOccurrenceKey =
            idInput.structuralOccurrenceKey;
        if (!semanticEdgeIds.contains(edge.id))
        {
            semanticEdgeIds.insert(edge.id);
            result.semanticGraph.edges.append(
                std::move(edge));
        }
    }

    const StoryGraphProjectRequest& request;
    const StoryGraphProjectResolver::ReadCallback&
        readCallback;
    const StoryGraphProjectResolver::
        MapFolderResolverCallback&
            mapFolderResolver;
    const StoryGraphProjectResolver::CancelCallback&
        cancelCallback;
    StoryGraphProjectResult result;
    StoryGraphSourceSnapshot preparedEntrySource;
    QHash<QString, StoryGraphSourceSnapshot>
        openSnapshots;
    QHash<QString, CachedRead> readCache;
    QHash<QString, int> documentIndexes;
    QList<DocumentFlowIndex>
        documentFlowIndexes;
    QHash<QString, InvocationState> invocations;
    QList<PendingFlowState> pendingFlowStates;
    qsizetype nextPendingFlowStateIndex = 0;
    QHash<QString, QSet<QString>>
        acceptedFlowContexts;
    QSet<QString> collapsedFlowBuckets;
    QSet<QString> scheduledDetachedComponents;
    QSet<QString> flowWarningKeys;
    QHash<QString, PendingResolutionRecord>
        pendingResolutionRecords;
    QStringList pendingResolutionOrder;
    QSet<QString> controlNodeIds;
    QSet<QString> controlEdgeIds;
    QSet<QString> semanticNodeIds;
    QSet<QString> semanticEdgeIds;
};
}

bool StoryGraphProjectResult::hasUsableGraph() const
{
    return status ==
            StoryGraphProjectStatus::Complete ||
        status ==
            StoryGraphProjectStatus::Partial;
}

bool StoryGraphProjectResult::wasCancelled() const
{
    return status ==
        StoryGraphProjectStatus::Cancelled;
}

StoryGraphProjectResult
StoryGraphProjectResolver::analyze(
    const StoryGraphProjectRequest& request,
    const ReadCallback& readCallback,
    const MapFolderResolverCallback&
        mapFolderResolver,
    const CancelCallback& cancelCallback)
{
    Resolver resolver(
        request,
        readCallback,
        mapFolderResolver,
        cancelCallback);
    return resolver.run();
}

bool StoryGraphProjectResolver::
    isStrictRelativeVirtualPath(
        const QString& value,
        QString* rejectionReason)
{
    return strictPath(
        value,
        rejectionReason);
}

QString storyGraphContentRootKindToString(
    StoryGraphContentRootKind kind)
{
    switch (kind)
    {
    case StoryGraphContentRootKind::Active:
        return QStringLiteral("active");
    case StoryGraphContentRootKind::DependencyId:
        return QStringLiteral("dependency-id");
    case StoryGraphContentRootKind::Common:
        return QStringLiteral("common");
    }
    return QStringLiteral("invalid");
}

QString makeStoryGraphPortableRootKey(
    StoryGraphContentRootKind kind,
    quint64 ordinal,
    const QString& resourcePackId)
{
    const QByteArray escapedIdentity =
        QUrl::toPercentEncoding(resourcePackId);
    return QStringLiteral("story-root-v1/%1/%2/%3")
        .arg(storyGraphContentRootKindToString(kind))
        .arg(ordinal)
        .arg(QString::fromLatin1(escapedIdentity));
}

QString storyGraphMapContextStateToString(
    StoryGraphMapContextState state)
{
    switch (state)
    {
    case StoryGraphMapContextState::Known:
        return QStringLiteral("known");
    case StoryGraphMapContextState::Assumed:
        return QStringLiteral("assumed");
    case StoryGraphMapContextState::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("invalid");
}

QString storyGraphTargetResolutionStatusToString(
    StoryGraphTargetResolutionStatus status)
{
    switch (status)
    {
    case StoryGraphTargetResolutionStatus::Resolved:
        return QStringLiteral("resolved");
    case StoryGraphTargetResolutionStatus::Missing:
        return QStringLiteral("missing");
    case StoryGraphTargetResolutionStatus::Rejected:
        return QStringLiteral("rejected");
    case StoryGraphTargetResolutionStatus::ReadError:
        return QStringLiteral("read-error");
    case StoryGraphTargetResolutionStatus::Cancelled:
        return QStringLiteral("cancelled");
    case StoryGraphTargetResolutionStatus::
            ContextDependent:
        return QStringLiteral("context-dependent");
    case StoryGraphTargetResolutionStatus::
            BudgetExceeded:
        return QStringLiteral("budget-exceeded");
    }
    return QStringLiteral("invalid");
}

QString storyGraphProjectStatusToString(
    StoryGraphProjectStatus status)
{
    switch (status)
    {
    case StoryGraphProjectStatus::Complete:
        return QStringLiteral("complete");
    case StoryGraphProjectStatus::Partial:
        return QStringLiteral("partial");
    case StoryGraphProjectStatus::Failed:
        return QStringLiteral("failed");
    case StoryGraphProjectStatus::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("invalid");
}
