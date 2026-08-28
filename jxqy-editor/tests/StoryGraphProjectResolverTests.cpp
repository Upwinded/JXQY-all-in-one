#include "../core/StoryGraphProjectResolver.h"

#include <QCoreApplication>
#include <QHash>
#include <QStringList>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

StoryGraphProjectRequest makeRequest(
    const QString& source,
    const QString& virtualPath =
        QStringLiteral(
            "script/map/map_a/entry.txt"))
{
    StoryGraphProjectRequest request;
    request.entrySource.identity.portableRootKey =
        QStringLiteral("active");
    request.entrySource.identity.virtualPath =
        virtualPath;
    request.entrySource.identity.fromEditorBuffer =
        true;
    request.entrySource.identity.documentRevision = 7;
    request.entrySource.utf8Bytes = source.toUtf8();
    request.orderedContentRoots = {
        {StoryGraphContentRootKind::Active,
         QStringLiteral("active"),
         0},
        {StoryGraphContentRootKind::DependencyId,
         QStringLiteral("dependency:test"),
         1},
        {StoryGraphContentRootKind::Common,
         QStringLiteral("common"),
         2}
    };
    request.entryMapContext.state =
        StoryGraphMapContextState::Known;
    request.entryMapContext.effectiveMapFolder =
        QStringLiteral("map_a");
    request.analysisGeneration = 19;
    return request;
}

const StoryGraphTargetResolution* findResolution(
    const StoryGraphProjectResult& result,
    const QString& literalTarget,
    const QString& targetVirtualPath = QString())
{
    for (const StoryGraphTargetResolution& resolution :
         result.targetResolutions)
    {
        if (resolution.literalTarget != literalTarget)
            continue;
        if (!targetVirtualPath.isEmpty() &&
            resolution.targetVirtualPath !=
                targetVirtualPath)
        {
            continue;
        }
        return &resolution;
    }
    return nullptr;
}

const StoryGraphDocumentResult* findDocument(
    const StoryGraphProjectResult& result,
    const QString& portableRootKey,
    const QString& virtualPath)
{
    for (const StoryGraphDocumentResult& document :
         result.documents)
    {
        if (document.source.portableRootKey ==
                portableRootKey &&
            document.source.virtualPath ==
                virtualPath)
        {
            return &document;
        }
    }
    return nullptr;
}

const StoryGraphNode* findResolutionPlaceholder(
    const StoryGraphProjectResult& result,
    const QString& literalTarget)
{
    for (const StoryGraphNode& node :
         result.semanticGraph.nodes)
    {
        if (node.literalTarget == literalTarget &&
            (node.kind ==
                 StoryGraphNodeKind::MissingTarget ||
             node.kind ==
                 StoryGraphNodeKind::Warning))
        {
            return &node;
        }
    }
    return nullptr;
}

bool hasWarningCode(
    const StoryGraphProjectResult& result,
    const QString& diagnosticCode)
{
    for (const StoryGraphWarning& warning :
         result.warnings)
    {
        if (warning.diagnosticCode ==
            diagnosticCode)
        {
            return true;
        }
    }
    return false;
}

bool hasEdgeKind(
    const StoryGraphResult& graph,
    StoryGraphEdgeKind kind)
{
    for (const StoryGraphEdge& edge : graph.edges)
    {
        if (edge.kind == kind)
            return true;
    }
    return false;
}

bool testStrictVirtualPaths()
{
    bool passed = true;
    QString reason;
    passed &= check(
        StoryGraphProjectResolver::
            isStrictRelativeVirtualPath(
                QStringLiteral(
                    "script/map/龙 门/事件 1.txt"),
                &reason) &&
        reason.isEmpty(),
        "strict virtual path accepts Unicode and spaces");
    const QStringList rejectedPaths =
    {
        QStringLiteral("../escape.txt"),
        QStringLiteral("./same.txt"),
        QStringLiteral("script//same.txt"),
        QStringLiteral("/absolute.txt"),
        QStringLiteral("C:/drive.txt"),
        QStringLiteral("script\\windows.txt"),
        QStringLiteral("script/CON.txt"),
        QStringLiteral("script/LPT1.lua"),
        QStringLiteral("script/name."),
        QStringLiteral("script/name "),
        QStringLiteral("script/bad?.txt"),
        QStringLiteral("script/bad") +
            QChar(0x0085) +
            QStringLiteral(".txt"),
        QStringLiteral("script/") +
            QChar(0xD800) +
            QStringLiteral(".txt")
    };
    for (const QString& rejected :
         rejectedPaths)
    {
        passed &= check(
            !StoryGraphProjectResolver::
                isStrictRelativeVirtualPath(
                    rejected,
                    &reason) &&
            !reason.isEmpty(),
            "strict virtual path rejects unsafe or non-canonical spelling");
    }
    return passed;
}

bool testCategoryBeforeRootAndDirtySnapshot()
{
    StoryGraphProjectRequest request =
        makeRequest(
            QStringLiteral(
                "runscript(\"next.txt\")\n"
                "runscript(\"dirty.txt\")\n"));

    StoryGraphSourceSnapshot dirty;
    dirty.identity.portableRootKey =
        QStringLiteral("active");
    dirty.identity.virtualPath =
        QStringLiteral(
            "script/map/map_a/dirty.txt");
    dirty.identity.canonicalAbsolutePath =
        QStringLiteral(
            "C:/fixture/active/dirty.txt");
    dirty.identity.documentRevision = 42;
    dirty.identity.fromEditorBuffer = true;
    dirty.utf8Bytes =
        QByteArray("say(\"dirty-buffer\")\n");
    request.activeRootOpenSnapshots.append(dirty);

    QStringList reads;
    const StoryGraphProjectResult result =
        StoryGraphProjectResolver::analyze(
            request,
            [&reads](
                const StoryGraphContentRoot& root,
                const QString& virtualPath)
            {
                reads.append(
                    root.portableRootKey +
                    QLatin1Char('|') +
                    virtualPath);
                StoryGraphReadResult read;
                if (root.portableRootKey ==
                        QStringLiteral(
                            "dependency:test") &&
                    virtualPath ==
                        QStringLiteral(
                            "script/map/map_a/next.txt"))
                {
                    read.status =
                        StoryGraphReadStatus::Found;
                    read.utf8Bytes =
                        QByteArray(
                            "say(\"dependency-map\")\n");
                    read.canonicalAbsolutePath =
                        QStringLiteral(
                            "C:/fixture/dependency/next.txt");
                }
                return read;
            });

    const StoryGraphTargetResolution* next =
        findResolution(
            result,
            QStringLiteral("next.txt"));
    const StoryGraphTargetResolution* dirtyResolution =
        findResolution(
            result,
            QStringLiteral("dirty.txt"));
    const StoryGraphDocumentResult* dirtyDocument =
        findDocument(
            result,
            QStringLiteral("active"),
            QStringLiteral(
                "script/map/map_a/dirty.txt"));

    bool passed = true;
    passed &= check(
        result.status ==
            StoryGraphProjectStatus::Complete &&
        result.documents.size() == 3,
        "project resolver combines entry and two resolved documents");
    passed &= check(
        reads.size() >= 2 &&
        reads.at(0) ==
            QStringLiteral(
                "active|script/map/map_a/next.txt") &&
        reads.at(1) ==
            QStringLiteral(
                "dependency:test|script/map/map_a/next.txt"),
        "candidate category is outer and ordered root is inner");
    passed &= check(
        next != nullptr &&
        next->status ==
            StoryGraphTargetResolutionStatus::Resolved &&
        next->targetPortableRootKey ==
            QStringLiteral("dependency:test") &&
        next->targetVirtualPath ==
            QStringLiteral(
                "script/map/map_a/next.txt"),
        "dependency map candidate wins before active goods fallback");
    passed &= check(
        dirtyResolution != nullptr &&
        dirtyResolution->status ==
            StoryGraphTargetResolutionStatus::Resolved &&
        dirtyDocument != nullptr &&
        dirtyDocument->source.fromEditorBuffer &&
        dirtyDocument->source.documentRevision == 42,
        "exact active-root dirty snapshot overrides disk callback");
    passed &= check(
        !reads.contains(
            QStringLiteral(
                "active|script/map/map_a/dirty.txt")),
        "dirty snapshot is consumed without reading its disk path");
    passed &= check(
        hasEdgeKind(
            result.semanticGraph,
            StoryGraphEdgeKind::Call),
        "resolved serial calls add project-level call edges");
    return passed;
}

bool testZeroByteStopsRootFallbackWithinCategory()
{
    StoryGraphProjectRequest request =
        makeRequest(
            QStringLiteral(
                "runscript(\"zero.txt\")\n"));
    QStringList reads;
    const StoryGraphProjectResult result =
        StoryGraphProjectResolver::analyze(
            request,
            [&reads](
                const StoryGraphContentRoot& root,
                const QString& virtualPath)
            {
                reads.append(
                    root.portableRootKey +
                    QLatin1Char('|') +
                    virtualPath);
                StoryGraphReadResult read;
                if (root.portableRootKey ==
                        QStringLiteral("active") &&
                    virtualPath ==
                        QStringLiteral(
                            "script/map/map_a/zero.txt"))
                {
                    read.status =
                        StoryGraphReadStatus::Found;
                    return read;
                }
                if (root.portableRootKey ==
                        QStringLiteral(
                            "dependency:test") &&
                    virtualPath ==
                        QStringLiteral(
                            "script/map/map_a/zero.txt"))
                {
                    read.status =
                        StoryGraphReadStatus::Found;
                    read.utf8Bytes =
                        QByteArray(
                            "say(\"wrong-root\")\n");
                    return read;
                }
                if (root.portableRootKey ==
                        QStringLiteral("active") &&
                    virtualPath ==
                        QStringLiteral(
                            "script/goods/zero.txt"))
                {
                    read.status =
                        StoryGraphReadStatus::Found;
                    read.utf8Bytes =
                        QByteArray(
                            "say(\"goods\")\n");
                }
                return read;
            });

    const StoryGraphTargetResolution* resolution =
        findResolution(
            result,
            QStringLiteral("zero.txt"));
    bool passed = true;
    passed &= check(
        resolution != nullptr &&
        resolution->targetPortableRootKey ==
            QStringLiteral("active") &&
        resolution->targetVirtualPath ==
            QStringLiteral(
                "script/goods/zero.txt"),
        "zero-byte map hit advances to goods category");
    passed &= check(
        !reads.contains(
            QStringLiteral(
                "dependency:test|script/map/map_a/zero.txt")),
        "zero-byte hit never falls through to a later root in the same category");
    return passed;
}

bool testMapContextParallelAndLoadGame()
{
    StoryGraphProjectRequest request =
        makeRequest(
            QStringLiteral(
                "loadmap(\"new.map\")\n"
                "runparallelscript(\"after.txt\")\n"
                "loadgame(1)\n"
                "runscript(\"post.txt\")\n"));

    QStringList reads;
    const StoryGraphProjectResult result =
        StoryGraphProjectResolver::analyze(
            request,
            [&reads](
                const StoryGraphContentRoot& root,
                const QString& virtualPath)
            {
                reads.append(
                    root.portableRootKey +
                    QLatin1Char('|') +
                    virtualPath);
                StoryGraphReadResult read;
                if (root.kind ==
                        StoryGraphContentRootKind::Active &&
                    (virtualPath ==
                         QStringLiteral(
                             "script/map/map_a/after.txt") ||
                     virtualPath ==
                         QStringLiteral(
                             "script/map/new_folder/after.txt") ||
                     virtualPath ==
                         QStringLiteral(
                             "script/common/post.txt")))
                {
                    read.status =
                        StoryGraphReadStatus::Found;
                    read.utf8Bytes =
                        QByteArray("say(\"target\")\n");
                }
                return read;
            },
            [](const QString& mapTarget)
            {
                StoryGraphMapFolderResolution resolution;
                if (mapTarget ==
                    QStringLiteral("new.map"))
                {
                    resolution.status =
                        StoryGraphMapFolderResolutionStatus::
                            Resolved;
                    resolution.effectiveMapFolder =
                        QStringLiteral("new_folder");
                }
                return resolution;
            });

    bool sawNewFolderParallel = false;
    bool sawOldFolderParallel = false;
    bool sawUnknownPost = false;
    for (const StoryGraphTargetResolution& resolution :
         result.targetResolutions)
    {
        if (resolution.literalTarget ==
            QStringLiteral("after.txt"))
        {
            sawNewFolderParallel |=
                resolution.edgeKind ==
                    StoryGraphEdgeKind::ParallelCall &&
                resolution.mapContext.effectiveMapFolder ==
                    QStringLiteral("new_folder");
            sawOldFolderParallel |=
                resolution.edgeKind ==
                    StoryGraphEdgeKind::ParallelCall &&
                resolution.mapContext.effectiveMapFolder ==
                    QStringLiteral("map_a");
        }
        if (resolution.literalTarget ==
            QStringLiteral("post.txt"))
        {
            sawUnknownPost |=
                resolution.mapContext.state ==
                    StoryGraphMapContextState::Unknown &&
                resolution.targetVirtualPath ==
                    QStringLiteral(
                        "script/common/post.txt");
        }
    }

    bool postTriedMap = false;
    for (const QString& read : reads)
    {
        postTriedMap |=
            read.contains(
                QStringLiteral("/post.txt")) &&
            read.contains(
                QStringLiteral("|script/map/"));
    }

    bool passed = true;
    passed &= check(
        sawNewFolderParallel &&
        sawOldFolderParallel,
        "loadmap preserves failure branch and adds resolved map-folder branch");
    passed &= check(
        sawUnknownPost &&
        !postTriedMap,
        "loadgame invalidates map context before later script resolution");
    passed &= check(
        hasEdgeKind(
            result.semanticGraph,
            StoryGraphEdgeKind::ParallelCall),
        "parallel calls remain distinct in the combined graph");
    return passed;
}

bool testControlFlowScopedMapContexts()
{
    const auto reader =
        [](
            const StoryGraphContentRoot& root,
            const QString& virtualPath)
        {
            StoryGraphReadResult read;
            if (root.kind ==
                    StoryGraphContentRootKind::Active &&
                virtualPath ==
                    QStringLiteral(
                        "script/map/map_a/after.txt"))
            {
                read.status =
                    StoryGraphReadStatus::Found;
                read.utf8Bytes =
                    QByteArray("return\n");
            }
            return read;
        };

    StoryGraphProjectRequest dormantRequest =
        makeRequest(
            QStringLiteral(
                "function dormant()\n"
                "    loadgame(1)\n"
                "end\n"
                "runscript(\"after.txt\")\n"));
    const StoryGraphProjectResult dormantResult =
        StoryGraphProjectResolver::analyze(
            dormantRequest,
            reader);
    const StoryGraphTargetResolution*
        dormantResolution =
            findResolution(
                dormantResult,
                QStringLiteral("after.txt"),
                QStringLiteral(
                    "script/map/map_a/after.txt"));

    StoryGraphProjectRequest branchRequest =
        makeRequest(
            QStringLiteral(
                "if getvar(\"flag\") then\n"
                "    loadgame(1)\n"
                "end\n"
                "runscript(\"after.txt\")\n"));
    const StoryGraphProjectResult branchResult =
        StoryGraphProjectResolver::analyze(
            branchRequest,
            reader);
    bool branchResolvedKnownMap = false;
    bool branchRetainedUnknownMap = false;
    for (const StoryGraphTargetResolution& resolution :
         branchResult.targetResolutions)
    {
        if (resolution.literalTarget !=
            QStringLiteral("after.txt"))
        {
            continue;
        }
        branchResolvedKnownMap |=
            resolution.targetVirtualPath ==
                QStringLiteral(
                    "script/map/map_a/after.txt");
        branchRetainedUnknownMap |=
            resolution.mapContext.state ==
                StoryGraphMapContextState::Unknown;
    }

    bool passed = true;
    passed &= check(
        dormantResolution != nullptr &&
        dormantResult.documents.size() == 2,
        "an uncalled function cannot invalidate the chunk map context");
    passed &= check(
        branchResolvedKnownMap &&
        branchRetainedUnknownMap &&
        branchResult.documents.size() == 2,
        "a conditional loadgame preserves both bypass and invalidated map contexts");
    return passed;
}

bool testSerialAndParallelMapContextPropagation()
{
    const auto reader =
        [](
            const StoryGraphContentRoot& root,
            const QString& virtualPath)
        {
            StoryGraphReadResult read;
            if (root.kind !=
                StoryGraphContentRootKind::Active)
            {
                return read;
            }
            if (virtualPath ==
                    QStringLiteral(
                        "script/map/map_a/switch.txt"))
            {
                read.status =
                    StoryGraphReadStatus::Found;
                read.utf8Bytes =
                    QByteArray(
                        "loadmap(\"new.map\")\n");
            }
            else if (
                virtualPath ==
                    QStringLiteral(
                        "script/map/map_a/after.txt") ||
                virtualPath ==
                    QStringLiteral(
                        "script/map/map_b/after.txt"))
            {
                read.status =
                    StoryGraphReadStatus::Found;
                read.utf8Bytes =
                    QByteArray("return\n");
            }
            return read;
        };
    const auto mapFolderResolver =
        [](const QString& mapTarget)
        {
            StoryGraphMapFolderResolution resolution;
            if (mapTarget ==
                QStringLiteral("new.map"))
            {
                resolution.status =
                    StoryGraphMapFolderResolutionStatus::
                        Resolved;
                resolution.effectiveMapFolder =
                    QStringLiteral("map_b");
            }
            return resolution;
        };

    const StoryGraphProjectResult serialResult =
        StoryGraphProjectResolver::analyze(
            makeRequest(
                QStringLiteral(
                    "runscript(\"switch.txt\")\n"
                    "runscript(\"after.txt\")\n")),
            reader,
            mapFolderResolver);
    bool serialSawOriginalMap = false;
    bool serialSawChangedMap = false;
    for (const StoryGraphTargetResolution& resolution :
         serialResult.targetResolutions)
    {
        if (resolution.literalTarget !=
            QStringLiteral("after.txt"))
        {
            continue;
        }
        serialSawOriginalMap |=
            resolution.targetVirtualPath ==
                QStringLiteral(
                    "script/map/map_a/after.txt");
        serialSawChangedMap |=
            resolution.targetVirtualPath ==
                QStringLiteral(
                    "script/map/map_b/after.txt");
    }

    const StoryGraphProjectResult parallelResult =
        StoryGraphProjectResolver::analyze(
            makeRequest(
                QStringLiteral(
                    "runparallelscript(\"switch.txt\")\n"
                    "runscript(\"after.txt\")\n")),
            reader,
            mapFolderResolver);
    bool parallelSawOriginalMap = false;
    bool parallelSawChangedMap = false;
    for (const StoryGraphTargetResolution& resolution :
         parallelResult.targetResolutions)
    {
        if (resolution.literalTarget !=
            QStringLiteral("after.txt"))
        {
            continue;
        }
        parallelSawOriginalMap |=
            resolution.targetVirtualPath ==
                QStringLiteral(
                    "script/map/map_a/after.txt");
        parallelSawChangedMap |=
            resolution.targetVirtualPath ==
                QStringLiteral(
                    "script/map/map_b/after.txt");
    }

    bool passed = true;
    passed &= check(
        serialSawOriginalMap &&
        serialSawChangedMap,
        "a serial child returns both loadmap failure and changed-map contexts");
    passed &= check(
        parallelSawOriginalMap &&
        !parallelSawChangedMap,
        "a parallel child captures its context without synchronously changing the caller");
    return passed;
}

bool testDynamicLoadGameAndLoopFixedPoint()
{
    const auto reader =
        [](
            const StoryGraphContentRoot& root,
            const QString& virtualPath)
        {
            StoryGraphReadResult read;
            if (root.kind !=
                StoryGraphContentRootKind::Active)
            {
                return read;
            }
            if (virtualPath ==
                    QStringLiteral(
                        "script/map/map_a/after.txt") ||
                virtualPath ==
                    QStringLiteral(
                        "script/map/map_b/after.txt"))
            {
                read.status =
                    StoryGraphReadStatus::Found;
                read.utf8Bytes =
                    QByteArray("return\n");
            }
            return read;
        };
    const auto mapFolderResolver =
        [](const QString& mapTarget)
        {
            StoryGraphMapFolderResolution resolution;
            if (mapTarget ==
                QStringLiteral("new.map"))
            {
                resolution.status =
                    StoryGraphMapFolderResolutionStatus::
                        Resolved;
                resolution.effectiveMapFolder =
                    QStringLiteral("map_b");
            }
            return resolution;
        };

    const StoryGraphProjectResult loadGameResult =
        StoryGraphProjectResolver::analyze(
            makeRequest(
                QStringLiteral(
                    "if getvar(\"restore\") then\n"
                    "    loadgame(slot, \"ignored\")\n"
                    "end\n"
                    "runscript(\"after.txt\")\n")),
            reader);
    bool loadGameKeptBypassContext = false;
    bool loadGameInvalidatedDynamicContext = false;
    for (const StoryGraphTargetResolution& resolution :
         loadGameResult.targetResolutions)
    {
        if (resolution.literalTarget !=
            QStringLiteral("after.txt"))
        {
            continue;
        }
        loadGameKeptBypassContext |=
            resolution.targetVirtualPath ==
                QStringLiteral(
                    "script/map/map_a/after.txt");
        loadGameInvalidatedDynamicContext |=
            resolution.mapContext.state ==
                StoryGraphMapContextState::Unknown;
    }

    const StoryGraphProjectResult loopResult =
        StoryGraphProjectResolver::analyze(
            makeRequest(
                QStringLiteral(
                    "while getvar(\"again\") do\n"
                    "    loadmap(\"new.map\")\n"
                    "end\n"
                    "runscript(\"after.txt\")\n")),
            reader,
            mapFolderResolver);
    bool loopKeptZeroIterationContext = false;
    bool loopReachedChangedContext = false;
    for (const StoryGraphTargetResolution& resolution :
         loopResult.targetResolutions)
    {
        if (resolution.literalTarget !=
            QStringLiteral("after.txt"))
        {
            continue;
        }
        loopKeptZeroIterationContext |=
            resolution.mapContext.state ==
                StoryGraphMapContextState::Known &&
            resolution.targetVirtualPath ==
                QStringLiteral(
                    "script/map/map_a/after.txt");
        loopReachedChangedContext |=
            resolution.mapContext.effectiveMapFolder ==
                QStringLiteral("map_b") &&
            resolution.targetVirtualPath ==
                QStringLiteral(
                    "script/map/map_b/after.txt");
    }

    bool passed = true;
    passed &= check(
        loadGameKeptBypassContext &&
        loadGameInvalidatedDynamicContext,
        "a direct valid-arity dynamic loadgame with ignored trailing arguments invalidates only its CFG branch");
    passed &= check(
        loopKeptZeroIterationContext &&
        loopReachedChangedContext,
        "loop map contexts reach a fixed point while preserving the zero-iteration branch");
    return passed;
}

bool testOpaqueSerialContinuationIsConservative()
{
    const auto reader =
        [](
            const StoryGraphContentRoot& root,
            const QString& virtualPath)
        {
            StoryGraphReadResult read;
            if (root.kind !=
                StoryGraphContentRootKind::Active)
            {
                return read;
            }
            if (virtualPath ==
                    QStringLiteral(
                        "script/map/map_a/wrapper.txt"))
            {
                read.status =
                    StoryGraphReadStatus::Found;
                read.utf8Bytes =
                    QByteArray(
                        "runscript(\"deep.txt\")\n"
                        "runscript(\"after.txt\")\n");
            }
            else if (
                virtualPath ==
                QStringLiteral(
                    "script/common/after.txt"))
            {
                read.status =
                    StoryGraphReadStatus::Found;
                read.utf8Bytes =
                    QByteArray("return\n");
            }
            return read;
        };

    const StoryGraphProjectResult dynamicResult =
        StoryGraphProjectResolver::analyze(
            makeRequest(
                QStringLiteral(
                    "runscript(target)\n"
                    "runscript(\"after.txt\")\n")),
            reader);
    const StoryGraphTargetResolution* dynamicCall =
        findResolution(
            dynamicResult,
            QString());
    const StoryGraphTargetResolution* dynamicAfter =
        findResolution(
            dynamicResult,
            QStringLiteral("after.txt"),
            QStringLiteral(
                "script/common/after.txt"));

    StoryGraphProjectRequest depthRequest =
        makeRequest(
            QStringLiteral(
                "runscript(\"wrapper.txt\")\n"));
    depthRequest.budget.maximumCallDepth = 1;
    const StoryGraphProjectResult depthResult =
        StoryGraphProjectResolver::analyze(
            depthRequest,
            reader);
    const StoryGraphTargetResolution* depthAfter =
        findResolution(
            depthResult,
            QStringLiteral("after.txt"));

    bool passed = true;
    passed &= check(
        dynamicCall != nullptr &&
        dynamicCall->status ==
            StoryGraphTargetResolutionStatus::
                ContextDependent &&
        dynamicCall->mapContext.state ==
            StoryGraphMapContextState::Known,
        "dynamic serial relation retains its original resolution context");
    passed &= check(
        dynamicAfter != nullptr &&
        dynamicAfter->mapContext.state ==
            StoryGraphMapContextState::Unknown &&
        dynamicAfter->targetVirtualPath ==
            QStringLiteral(
                "script/common/after.txt"),
        "dynamic serial continuation cannot retain a definite map context");
    passed &= check(
        depthAfter != nullptr &&
        depthAfter->status ==
            StoryGraphTargetResolutionStatus::
                BudgetExceeded &&
        depthAfter->mapContext.state ==
            StoryGraphMapContextState::Unknown,
        "depth-bounded serial child conservatively invalidates the continuation map context");
    return passed;
}

bool testUnusableSerialTargetContinuesCaller()
{
    const StoryGraphProjectResult result =
        StoryGraphProjectResolver::analyze(
            makeRequest(
                QStringLiteral(
                    "runscript(\"broken.txt\")\n"
                    "runscript(\"after.txt\")\n")),
            [](
                const StoryGraphContentRoot& root,
                const QString& virtualPath)
            {
                StoryGraphReadResult read;
                if (root.kind !=
                    StoryGraphContentRootKind::Active)
                {
                    return read;
                }
                if (virtualPath ==
                        QStringLiteral(
                            "script/map/map_a/broken.txt"))
                {
                    read.status =
                        StoryGraphReadStatus::Found;
                    read.utf8Bytes =
                        QByteArray::fromHex(
                            "74616c6b28c0af29");
                }
                else if (
                    virtualPath ==
                    QStringLiteral(
                        "script/common/after.txt"))
                {
                    read.status =
                        StoryGraphReadStatus::Found;
                    read.utf8Bytes =
                        QByteArray("return\n");
                }
                return read;
            });

    const StoryGraphTargetResolution* broken =
        findResolution(
            result,
            QStringLiteral("broken.txt"));
    const StoryGraphTargetResolution* after =
        findResolution(
            result,
            QStringLiteral("after.txt"),
            QStringLiteral(
                "script/common/after.txt"));

    bool passed = true;
    passed &= check(
        broken != nullptr &&
        broken->status ==
            StoryGraphTargetResolutionStatus::
                ReadError &&
        hasWarningCode(
            result,
            QStringLiteral(
                "story_graph.project.read_error")),
        "unusable serial target forms an explicit read-error relation");
    passed &= check(
        after != nullptr &&
        after->mapContext.state ==
            StoryGraphMapContextState::Unknown &&
        result.documents.size() == 3,
        "unusable serial target continues the caller with conservative context");
    return passed;
}

bool testMapContextTrackingBoundary()
{
    const auto analyzeFanout =
        [](int mapLoadCount)
        {
            QString source;
            for (int index = 0;
                 index < mapLoadCount;
                 ++index)
            {
                source +=
                    QStringLiteral("loadmap(\"map_%1.map\")\n").
                        arg(index);
            }
            source +=
                QStringLiteral(
                    "runscript(\"after.txt\")\n");
            return StoryGraphProjectResolver::analyze(
                makeRequest(source),
                [](
                    const StoryGraphContentRoot&,
                    const QString&)
                {
                    return StoryGraphReadResult();
                });
        };

    const StoryGraphProjectResult atBoundary =
        analyzeFanout(
            63);
    int boundaryContextCount = 0;
    for (const StoryGraphTargetResolution& resolution :
         atBoundary.targetResolutions)
    {
        if (resolution.literalTarget ==
            QStringLiteral("after.txt"))
        {
            ++boundaryContextCount;
        }
    }

    const StoryGraphProjectResult beyondBoundary =
        analyzeFanout(
            64);
    int collapsedContextCount = 0;
    bool collapsedToUnknown = true;
    for (const StoryGraphTargetResolution& resolution :
         beyondBoundary.targetResolutions)
    {
        if (resolution.literalTarget !=
            QStringLiteral("after.txt"))
        {
            continue;
        }
        ++collapsedContextCount;
        collapsedToUnknown &=
            resolution.mapContext.state ==
                StoryGraphMapContextState::Unknown;
    }

    bool passed = true;
    passed &= check(
        boundaryContextCount == 64 &&
        !hasWarningCode(
            atBoundary,
            QStringLiteral(
                "story_graph.project.budget.map_contexts")),
        "exactly 64 map contexts remain accepted without collapse");
    passed &= check(
        collapsedContextCount == 1 &&
        collapsedToUnknown &&
        hasWarningCode(
            beyondBoundary,
            QStringLiteral(
                "story_graph.project.budget.map_contexts")),
        "the 65th map context collapses the bucket to one unknown context");
    return passed;
}

bool testReadBudgetsBeforeRetention()
{
    bool passed = true;

    StoryGraphProjectRequest singleFileRequest =
        makeRequest(
            QStringLiteral(
                "runparallelscript(\"large.txt\")\n"
                "runparallelscript(\"large.txt\")\n"));
    const QByteArray largeTarget(96, 'x');
    singleFileRequest.budget.maximumSingleFileBytes =
        std::max<qsizetype>(
            singleFileRequest.entrySource.
                utf8Bytes.size(),
            32);
    singleFileRequest.budget.maximumTotalBytes = 4096;
    int singleFileReadCount = 0;
    const StoryGraphProjectResult singleFileResult =
        StoryGraphProjectResolver::analyze(
            singleFileRequest,
            [&singleFileReadCount, &largeTarget](
                const StoryGraphContentRoot& root,
                const QString& virtualPath)
            {
                ++singleFileReadCount;
                StoryGraphReadResult read;
                if (root.kind ==
                        StoryGraphContentRootKind::Active &&
                    virtualPath ==
                        QStringLiteral(
                            "script/map/map_a/large.txt"))
                {
                    read.status =
                        StoryGraphReadStatus::Found;
                    read.utf8Bytes = largeTarget;
                }
                return read;
            });
    const StoryGraphTargetResolution* singleFile =
        findResolution(
            singleFileResult,
            QStringLiteral("large.txt"));
    int singleFileBudgetExceededCount = 0;
    for (const StoryGraphTargetResolution& resolution :
         singleFileResult.targetResolutions)
    {
        if (resolution.literalTarget ==
                QStringLiteral("large.txt") &&
            resolution.status ==
                StoryGraphTargetResolutionStatus::
                    BudgetExceeded)
        {
            ++singleFileBudgetExceededCount;
        }
    }
    passed &= check(
        singleFile != nullptr &&
        singleFile->status ==
            StoryGraphTargetResolutionStatus::
                BudgetExceeded &&
        singleFileBudgetExceededCount == 2 &&
        singleFileResult.documents.size() == 1 &&
        singleFileResult.totalBytesRead ==
            singleFileRequest.entrySource.
                utf8Bytes.size() &&
        singleFileReadCount == 2 &&
        singleFile->targetPortableRootKey.isEmpty() &&
        singleFile->targetVirtualPath.isEmpty(),
        "single-file budget rejects found bytes without caching them");
    passed &= check(
        hasWarningCode(
            singleFileResult,
            QStringLiteral(
                "story_graph.project.budget.single_file")),
        "single-file budget keeps its precise diagnostic code");

    StoryGraphProjectRequest totalBytesRequest =
        makeRequest(
            QStringLiteral(
                "runscript(\"total.txt\")\n"));
    const QByteArray totalTarget(
        "say(\"target bytes\")\n");
    totalBytesRequest.budget.maximumSingleFileBytes =
        4096;
    totalBytesRequest.budget.maximumTotalBytes =
        totalBytesRequest.entrySource.utf8Bytes.size() +
        totalTarget.size() - 1;
    const StoryGraphProjectResult totalBytesResult =
        StoryGraphProjectResolver::analyze(
            totalBytesRequest,
            [&totalTarget](
                const StoryGraphContentRoot& root,
                const QString& virtualPath)
            {
                StoryGraphReadResult read;
                if (root.kind ==
                        StoryGraphContentRootKind::Active &&
                    virtualPath ==
                        QStringLiteral(
                            "script/map/map_a/total.txt"))
                {
                    read.status =
                        StoryGraphReadStatus::Found;
                    read.utf8Bytes = totalTarget;
                }
                return read;
            });
    const StoryGraphTargetResolution* totalBytes =
        findResolution(
            totalBytesResult,
            QStringLiteral("total.txt"));
    passed &= check(
        totalBytes != nullptr &&
        totalBytes->status ==
            StoryGraphTargetResolutionStatus::
                BudgetExceeded &&
        totalBytesResult.documents.size() == 1 &&
        totalBytesResult.totalBytesRead ==
            totalBytesRequest.entrySource.
                utf8Bytes.size() &&
        hasWarningCode(
            totalBytesResult,
            QStringLiteral(
                "story_graph.project.budget.total_bytes")),
        "remaining total-byte budget rejects found bytes before retention");

    StoryGraphProjectRequest fileCountRequest =
        makeRequest(
            QStringLiteral(
                "runscript(\"count.txt\")\n"));
    fileCountRequest.budget.maximumFileCount = 1;
    int fileCountReadCount = 0;
    const StoryGraphProjectResult fileCountResult =
        StoryGraphProjectResolver::analyze(
            fileCountRequest,
            [&fileCountReadCount](
                const StoryGraphContentRoot&,
                const QString&)
            {
                ++fileCountReadCount;
                StoryGraphReadResult read;
                read.status =
                    StoryGraphReadStatus::Found;
                read.utf8Bytes =
                    QByteArray("say(\"unreachable\")\n");
                return read;
            });
    const StoryGraphTargetResolution* fileCount =
        findResolution(
            fileCountResult,
            QStringLiteral("count.txt"));
    passed &= check(
        fileCount != nullptr &&
        fileCount->status ==
            StoryGraphTargetResolutionStatus::
                BudgetExceeded &&
        fileCountResult.documents.size() == 1 &&
        fileCountReadCount == 0 &&
        hasWarningCode(
            fileCountResult,
            QStringLiteral(
                "story_graph.project.budget.file_count")),
        "file-count budget is checked before reading a new target");

    return passed;
}

bool testMissingReadErrorAndStablePlaceholders()
{
    const StoryGraphProjectRequest request =
        makeRequest(
            QStringLiteral(
                "runscript(\"error.txt\")\n"
                "runscript(\"missing.txt\")\n"));
    const auto reader =
        [](
            const StoryGraphContentRoot& root,
            const QString& virtualPath)
        {
            StoryGraphReadResult read;
            if (root.kind ==
                    StoryGraphContentRootKind::Active &&
                virtualPath ==
                    QStringLiteral(
                        "script/map/map_a/error.txt"))
            {
                read.status =
                    StoryGraphReadStatus::Error;
                read.message =
                    QStringLiteral("fixture-read-error");
            }
            return read;
        };

    const StoryGraphProjectResult first =
        StoryGraphProjectResolver::analyze(
            request,
            reader);
    const StoryGraphProjectResult repeated =
        StoryGraphProjectResolver::analyze(
            request,
            reader);
    const StoryGraphTargetResolution* missing =
        findResolution(
            first,
            QStringLiteral("missing.txt"));
    const StoryGraphTargetResolution* readError =
        findResolution(
            first,
            QStringLiteral("error.txt"));
    const StoryGraphNode* missingPlaceholder =
        findResolutionPlaceholder(
            first,
            QStringLiteral("missing.txt"));
    const StoryGraphNode* repeatedMissingPlaceholder =
        findResolutionPlaceholder(
            repeated,
            QStringLiteral("missing.txt"));
    const StoryGraphNode* errorPlaceholder =
        findResolutionPlaceholder(
            first,
            QStringLiteral("error.txt"));
    const StoryGraphNode* repeatedErrorPlaceholder =
        findResolutionPlaceholder(
            repeated,
            QStringLiteral("error.txt"));

    bool passed = true;
    passed &= check(
        missing != nullptr &&
        missing->status ==
            StoryGraphTargetResolutionStatus::
                ContextDependent &&
        missing->mapContext.state ==
            StoryGraphMapContextState::Unknown &&
        missing->candidateVirtualPath ==
            QStringLiteral(
                "script/common/missing.txt") &&
        readError != nullptr &&
        readError->status ==
            StoryGraphTargetResolutionStatus::ReadError &&
        readError->message ==
            QStringLiteral("fixture-read-error"),
        "unknown-context missing and read-error callbacks retain distinct exact statuses");
    passed &= check(
        missingPlaceholder != nullptr &&
        missingPlaceholder->kind ==
            StoryGraphNodeKind::MissingTarget &&
        errorPlaceholder != nullptr &&
        errorPlaceholder->kind ==
            StoryGraphNodeKind::Warning &&
        repeatedMissingPlaceholder != nullptr &&
        repeatedErrorPlaceholder != nullptr &&
        missingPlaceholder->id ==
            repeatedMissingPlaceholder->id &&
        errorPlaceholder->id ==
            repeatedErrorPlaceholder->id &&
        isStoryGraphNodeId(missingPlaceholder->id) &&
        isStoryGraphNodeId(errorPlaceholder->id),
        "missing and read-error placeholder IDs are stable");
    passed &= check(
        hasWarningCode(
            first,
            QStringLiteral(
                "story_graph.project.context_dependent")) &&
        hasWarningCode(
            first,
            QStringLiteral(
                "story_graph.project.read_error")),
        "context-dependent missing and read-error placeholders retain precise warnings");
    return passed;
}

bool testInvalidLoadMapTargetWarning()
{
    StoryGraphProjectRequest request =
        makeRequest(
            QStringLiteral(
                "loadmap(\"../escape.map\")\n"
                "runscript(\"after.txt\")\n"));
    const StoryGraphProjectResult result =
        StoryGraphProjectResolver::analyze(
            request,
            [](
                const StoryGraphContentRoot&,
                const QString&)
            {
                return StoryGraphReadResult();
            });

    bool sawOldAssumedContext = false;
    bool sawUnknownContext = false;
    for (const StoryGraphTargetResolution& resolution :
         result.targetResolutions)
    {
        if (resolution.literalTarget !=
            QStringLiteral("after.txt"))
        {
            continue;
        }
        sawOldAssumedContext |=
            resolution.mapContext.state ==
                StoryGraphMapContextState::Assumed &&
            resolution.mapContext.effectiveMapFolder ==
                QStringLiteral("map_a");
        sawUnknownContext |=
            resolution.mapContext.state ==
                StoryGraphMapContextState::Unknown &&
            resolution.mapContext.effectiveMapFolder.
                isEmpty();
    }

    bool foundPreciseWarning = false;
    for (const StoryGraphWarning& warning :
         result.warnings)
    {
        if (warning.diagnosticCode ==
            QStringLiteral(
                "story_graph.project.map_target_rejected"))
        {
            foundPreciseWarning =
                !warning.relatedNodeId.isEmpty() &&
                warning.sourceRange.isValid();
        }
    }

    bool passed = true;
    passed &= check(
        foundPreciseWarning,
        "invalid literal loadmap target has a stable precise warning");
    passed &= check(
        sawOldAssumedContext &&
        sawUnknownContext,
        "invalid loadmap retains the old assumed branch and an unknown branch");
    return passed;
}

bool testDepthBudgetRejectedTargetAndCancellation()
{
    StoryGraphProjectRequest request =
        makeRequest(
            QStringLiteral(
                "runscript(\"b.txt\")\n"
                "runscript(\"../escape.txt\")\n"),
            QStringLiteral(
                "script/map/map_a/a.txt"));
    request.budget.maximumCallDepth = 1;

    int callbackCount = 0;
    const StoryGraphProjectResult result =
        StoryGraphProjectResolver::analyze(
            request,
            [&callbackCount](
                const StoryGraphContentRoot& root,
                const QString& virtualPath)
            {
                ++callbackCount;
                StoryGraphReadResult read;
                if (root.kind ==
                        StoryGraphContentRootKind::Active &&
                    virtualPath ==
                        QStringLiteral(
                            "script/map/map_a/b.txt"))
                {
                    read.status =
                        StoryGraphReadStatus::Found;
                    read.utf8Bytes =
                        QByteArray(
                            "runscript(\"c.txt\")\n");
                }
                return read;
            });

    const StoryGraphTargetResolution* rejected =
        findResolution(
            result,
            QStringLiteral("../escape.txt"));
    const StoryGraphTargetResolution* budget =
        findResolution(
            result,
            QStringLiteral("c.txt"));
    bool passed = true;
    passed &= check(
        rejected != nullptr &&
        rejected->status ==
            StoryGraphTargetResolutionStatus::Rejected,
        "unsafe literal target is retained as a rejected relation");
    passed &= check(
        budget != nullptr &&
        budget->status ==
            StoryGraphTargetResolutionStatus::BudgetExceeded &&
        result.documents.size() == 2,
        "call-depth budget stops traversal without discarding usable documents");
    passed &= check(
        callbackCount >= 1,
        "safe resolved target uses the supplied reader");

    const StoryGraphProjectResult cancelled =
        StoryGraphProjectResolver::analyze(
            request,
            StoryGraphProjectResolver::ReadCallback(),
            StoryGraphProjectResolver::
                MapFolderResolverCallback(),
            []()
            {
                return true;
            });
    passed &= check(
        cancelled.wasCancelled() &&
        cancelled.documents.isEmpty() &&
        !cancelled.controlFlowGraph.complete &&
        !cancelled.semanticGraph.complete,
        "early project cancellation publishes no analyzed document");
    return passed;
}

bool testShortestTraversalDepthWins()
{
    StoryGraphProjectRequest request =
        makeRequest(
            QStringLiteral(
                "runparallelscript(\"detour.txt\")\n"
                "runscript(\"shared.txt\")\n"));
    request.budget.maximumCallDepth = 2;

    const QHash<QString, QByteArray> files = {
        {
            QStringLiteral(
                "script/map/map_a/detour.txt"),
            QByteArray(
                "runscript(\"shared.txt\")\n")
        },
        {
            QStringLiteral(
                "script/map/map_a/shared.txt"),
            QByteArray(
                "runscript(\"leaf.txt\")\n")
        },
        {
            QStringLiteral(
                "script/map/map_a/leaf.txt"),
            QByteArray("return\n")
        }
    };
    const StoryGraphProjectResult result =
        StoryGraphProjectResolver::analyze(
            request,
            [&files](
                const StoryGraphContentRoot& root,
                const QString& virtualPath)
            {
                StoryGraphReadResult read;
                if (root.kind !=
                        StoryGraphContentRootKind::
                            Active ||
                    !files.contains(virtualPath))
                {
                    return read;
                }
                read.status =
                    StoryGraphReadStatus::Found;
                read.utf8Bytes = files.value(
                    virtualPath);
                return read;
            });

    const StoryGraphDocumentResult* shared =
        findDocument(
            result,
            QStringLiteral("active"),
            QStringLiteral(
                "script/map/map_a/shared.txt"));
    const StoryGraphDocumentResult* leaf =
        findDocument(
            result,
            QStringLiteral("active"),
            QStringLiteral(
                "script/map/map_a/leaf.txt"));
    const StoryGraphNode* leafCaller = nullptr;
    if (shared)
    {
        for (const StoryGraphNode& node :
             shared->semanticGraph.nodes)
        {
            if (node.literalTarget ==
                QStringLiteral("leaf.txt"))
            {
                leafCaller = &node;
                break;
            }
        }
    }

    int leafResolutionCount = 0;
    bool leafResolutionIsResolved = true;
    for (const StoryGraphTargetResolution& resolution :
         result.targetResolutions)
    {
        if (resolution.literalTarget !=
            QStringLiteral("leaf.txt"))
        {
            continue;
        }
        ++leafResolutionCount;
        leafResolutionIsResolved &=
            resolution.status ==
                StoryGraphTargetResolutionStatus::
                    Resolved;
    }

    bool hasLeafEdge = false;
    if (leafCaller && leaf)
    {
        for (const StoryGraphEdge& edge :
             result.semanticGraph.edges)
        {
            if (edge.fromNodeId ==
                    leafCaller->id &&
                edge.toNodeId ==
                    leaf->semanticGraph.
                        entryNodeId &&
                edge.kind ==
                    StoryGraphEdgeKind::Call)
            {
                hasLeafEdge = true;
                break;
            }
        }
    }

    bool passed = true;
    passed &= check(
        result.documents.size() == 4 &&
        shared != nullptr &&
        leaf != nullptr,
        "a later shorter path expands the shared script within depth budget");
    passed &= check(
        leafResolutionCount == 1 &&
        leafResolutionIsResolved &&
        !hasWarningCode(
            result,
            QStringLiteral(
                "story_graph.project.budget.call")),
        "shortest-depth traversal leaves no stale budget placeholder or warning");
    passed &= check(
        hasLeafEdge,
        "shortest-depth traversal retains the shared-to-leaf call edge");
    return passed;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;
    passed &= testStrictVirtualPaths();
    passed &= testCategoryBeforeRootAndDirtySnapshot();
    passed &= testZeroByteStopsRootFallbackWithinCategory();
    passed &= testMapContextParallelAndLoadGame();
    passed &= testControlFlowScopedMapContexts();
    passed &=
        testSerialAndParallelMapContextPropagation();
    passed &= testDynamicLoadGameAndLoopFixedPoint();
    passed &=
        testOpaqueSerialContinuationIsConservative();
    passed &=
        testUnusableSerialTargetContinuesCaller();
    passed &= testMapContextTrackingBoundary();
    passed &= testReadBudgetsBeforeRetention();
    passed &=
        testMissingReadErrorAndStablePlaceholders();
    passed &= testInvalidLoadMapTargetWarning();
    passed &= testDepthBudgetRejectedTargetAndCancellation();
    passed &= testShortestTraversalDepthWins();
    if (passed)
    {
        std::cout
            << "All story graph project resolver tests passed\n";
    }
    return passed ? 0 : 1;
}
