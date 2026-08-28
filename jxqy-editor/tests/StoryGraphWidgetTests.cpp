#include "../ui/MainWindow.h"
#include "../ui/DesktopRunPanel.h"
#include "../ui/ScriptEditorWindow.h"
#include "../ui/StoryGraphView.h"
#include "../ui/StoryGraphWindow.h"
#include "../core/DesktopRunSessionCoordinator.h"
#include "../core/DesktopRunSessionBase.h"
#include "../core/ProjectManager.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPushButton>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QTreeWidget>
#include <QToolButton>
#include <QVariant>

#include <algorithm>
#include <iostream>
#include <optional>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool writeFile(
    const QString& path,
    const QByteArray& bytes)
{
    const QFileInfo information(path);
    if (!QDir().mkpath(
            information.absolutePath()))
    {
        return false;
    }
    QFile file(path);
    return file.open(
               QIODevice::WriteOnly |
               QIODevice::Truncate) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

bool appendFile(
    const QString& path,
    const QByteArray& bytes)
{
    QFile file(path);
    return file.open(
               QIODevice::WriteOnly |
               QIODevice::Append) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

QByteArray jsonLine(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(
               QJsonDocument::Compact) +
        '\n';
}

std::optional<DesktopRunSessionRequest>
makeDesktopRunSessionRequest(
    const QString& executablePath,
    const QString& sessionsBase,
    const QString& formalRoot,
    const QString& mode)
{
    const QString mapPath =
        QDir(formalRoot).filePath(
            QStringLiteral(
                "map/ui-session.map"));
    const QString scriptPath =
        QDir(formalRoot).filePath(
            QStringLiteral(
                "script/ui-session.txt"));
    if (!writeFile(mapPath, QByteArray("map")) ||
        !writeFile(scriptPath, QByteArray("return")))
    {
        return std::nullopt;
    }

    PreparedSavedSceneLaunch prepared;
    prepared.scene.id =
        QStringLiteral("ui-session-") + mode;
    prepared.scene.name =
        QStringLiteral("UI session ") + mode;
    prepared.scene.mapPath =
        QStringLiteral("map/ui-session.map");
    prepared.scene.entryScriptPath =
        QStringLiteral("script/ui-session.txt");
    prepared.scene.playerPosition = QPoint(1, 1);
    prepared.assetsCollectionRoot = formalRoot;
    prepared.canonicalActiveResourcePackId =
        QStringLiteral("MOD");
    prepared.activeContentRoot = formalRoot;
    prepared.formalRoots = {formalRoot};

    DesktopRunSessionRequest request;
    request.executablePath = executablePath;
    request.trustedSessionsBaseDirectory =
        sessionsBase;
    request.preparedLaunch = std::move(prepared);
    return request;
}

ScriptEditorWindow* findScriptWindow(
    MainWindow& mainWindow,
    const QString& filePath)
{
    const QString expectedPath =
        QFileInfo(filePath).canonicalFilePath();
    for (ScriptEditorWindow* window :
         mainWindow.findChildren<
             ScriptEditorWindow*>())
    {
        if (window &&
            QFileInfo(window->currentFilePath()).
                canonicalFilePath() ==
                expectedPath)
        {
            return window;
        }
    }
    return nullptr;
}

const StoryGraphNode* findDialogueNode(
    const StoryGraphProjectResult& result,
    const QString& filePath)
{
    const QString expectedPath =
        QFileInfo(filePath).canonicalFilePath();
    const auto node = std::find_if(
        result.semanticGraph.nodes.cbegin(),
        result.semanticGraph.nodes.cend(),
        [&expectedPath](
            const StoryGraphNode& candidate)
        {
            return candidate.kind ==
                    StoryGraphNodeKind::Dialogue &&
                QFileInfo(
                    candidate.source.
                        canonicalAbsolutePath).
                    canonicalFilePath() ==
                    expectedPath;
        });
    return node ==
            result.semanticGraph.nodes.cend()
        ? nullptr
        : &*node;
}

bool requestSourceNavigationWithoutBlocking(
    StoryGraphWindow& graphWindow,
    const StoryGraphNode& node)
{
    bool unexpectedModal = false;
    QTimer modalGuard;
    QObject::connect(
        &modalGuard,
        &QTimer::timeout,
        [&unexpectedModal]()
        {
            auto* messageBox =
                qobject_cast<QMessageBox*>(
                    qApp->activeModalWidget());
            if (!messageBox)
                return;
            unexpectedModal = true;
            messageBox->reject();
        });
    modalGuard.start(5);
    graphWindow.sourceNavigationRequested(node);
    modalGuard.stop();
    return !unexpectedModal;
}

bool cursorIsAtMarker(
    ScriptEditorWindow& window,
    int oneBasedLine,
    const QString& marker)
{
    auto* editor =
        window.findChild<ScriptEditor*>();
    if (!editor || oneBasedLine <= 0)
        return false;
    const QTextBlock block =
        editor->document()->
            findBlockByLineNumber(
                oneBasedLine - 1);
    if (!block.isValid())
        return false;
    const int markerOffset =
        block.text().indexOf(marker);
    return markerOffset >= 0 &&
        editor->textCursor().position() ==
            block.position() + markerOffset;
}

StoryGraphResult makeGraph(int nodeCount)
{
    StoryGraphResult graph;
    graph.kind = StoryGraphKind::StorySemantics;
    for (int index = 0; index < nodeCount; ++index)
    {
        StoryGraphNode node;
        node.id =
            QStringLiteral("node-%1").arg(index);
        node.kind =
            index % 2 == 0
            ? StoryGraphNodeKind::Dialogue
            : StoryGraphNodeKind::Choice;
        node.title =
            QStringLiteral("Node %1").arg(index);
        node.summary =
            index == nodeCount / 2
            ? QStringLiteral(
                  "unique-search-marker")
            : QStringLiteral("summary");
        node.source.virtualPath =
            QStringLiteral(
                "script/map/test/story.txt");
        graph.nodes.append(std::move(node));
    }
    if (!graph.nodes.isEmpty())
    {
        graph.entryNodeId =
            graph.nodes.constFirst().id;
        graph.exitNodeId =
            graph.nodes.constLast().id;
    }
    for (int index = 1; index < nodeCount; ++index)
    {
        StoryGraphEdge edge;
        edge.id =
            QStringLiteral("edge-%1").arg(index);
        edge.fromNodeId =
            graph.nodes[index - 1].id;
        edge.toNodeId =
            graph.nodes[index].id;
        edge.kind =
            StoryGraphEdgeKind::Sequential;
        edge.label =
            QStringLiteral("edge");
        graph.edges.append(std::move(edge));
    }
    return graph;
}

StoryGraphLayoutResult makeLayout(
    const StoryGraphResult& graph)
{
    StoryGraphLayoutResult layout;
    layout.status =
        StoryGraphLayoutStatus::Complete;
    for (int index = 0;
         index < graph.nodes.size();
         ++index)
    {
        const QRectF rectangle(
            (index % 12) * 220.0,
            (index / 12) * 110.0,
            180.0,
            76.0);
        layout.nodeRectangles.insert(
            graph.nodes[index].id,
            rectangle);
        layout.bounds =
            layout.bounds.united(rectangle);
    }
    for (const StoryGraphEdge& edge :
         graph.edges)
    {
        StoryGraphEdgePlacement placement;
        placement.edgeId = edge.id;
        placement.fromNodeId =
            edge.fromNodeId;
        placement.toNodeId =
            edge.toNodeId;
        const QRectF from =
            layout.nodeRectangles.value(
                edge.fromNodeId);
        const QRectF to =
            layout.nodeRectangles.value(
                edge.toNodeId);
        placement.pathPoints = {
            from.center(),
            QPointF(
                to.center().x(),
                from.center().y()),
            to.center()
        };
        placement.routed = true;
        layout.edgePlacements.append(
            std::move(placement));
    }
    return layout;
}

bool waitForPresentation(
    StoryGraphView& view,
    int maximumMilliseconds = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!view.presentationComplete() &&
           timer.elapsed() < maximumMilliseconds)
    {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents,
            20);
    }
    return view.presentationComplete();
}

bool testBatchedPresentationAndReplacement()
{
    StoryGraphView view;
    QList<int> batchWorkCounts;
    QList<int> batchItemCounts;
    int previousSceneItemCount = 0;
    QObject::connect(
        &view,
        &StoryGraphView::
            presentationBatchProcessed,
        [&view,
         &batchWorkCounts,
         &batchItemCounts,
         &previousSceneItemCount](
            int workItemCount)
        {
            batchWorkCounts.append(
                workItemCount);
            const int sceneItemCount =
                view.scene()->items().size();
            batchItemCounts.append(
                sceneItemCount -
                previousSceneItemCount);
            previousSceneItemCount =
                sceneItemCount;
        });
    const StoryGraphResult large =
        makeGraph(420);
    const StoryGraphLayoutResult largeLayout =
        makeLayout(large);
    view.setGraph(large, largeLayout, 41);
    StoryGraphTraceGraphOverlay initialOverlay;
    initialOverlay.epoch = 1;
    initialOverlay.revision = 1;
    initialOverlay.executedNodeIds = {
        QStringLiteral("node-0"),
        QStringLiteral("node-1")
    };
    initialOverlay.executedEdgeIds = {
        QStringLiteral("edge-1")
    };
    StoryGraphTraceGraphOverlay::Delta
        initialDelta;
    initialDelta.revision = 1;
    initialDelta.addedNodeIds = {
        QStringLiteral("node-0"),
        QStringLiteral("node-1")
    };
    initialDelta.addedEdgeIds = {
        QStringLiteral("edge-1")
    };
    initialOverlay.deltas.append(
        initialDelta);
    view.setTraceOverlay(initialOverlay, 41);

    bool passed = true;
    passed &= check(
        view.presentedNodeCount() == 0 &&
        !view.presentationComplete(),
        "graph items are deferred instead of being built synchronously");
    passed &= check(
        waitForPresentation(view) &&
        view.presentedNodeCount() == 420 &&
        view.generation() == 41 &&
        view.executedNodeCount() == 2 &&
        view.executedEdgeCount() == 1,
        "all deferred node batches publish for the current generation");
    passed &= check(
        batchWorkCounts.size() > 1 &&
        std::all_of(
            batchWorkCounts.cbegin(),
            batchWorkCounts.cend(),
            [](int workItemCount)
            {
                return workItemCount > 0 &&
                    workItemCount <= 180;
            }),
        "node, edge-index, and edge presentation work stays bounded per batch");
    passed &= check(
        batchItemCounts.size() ==
            batchWorkCounts.size() &&
        std::all_of(
            batchItemCounts.cbegin(),
            batchItemCounts.cend(),
            [](int itemCount)
            {
                return itemCount >= 0 &&
                    itemCount <= 250;
            }),
        "each presentation batch creates at most 250 graphics items, including labeled edges");

    view.setAllowedNodeKinds(
        {StoryGraphNodeKind::Dialogue});
    int visibleSceneItemCount = 0;
    for (QGraphicsItem* item :
         view.scene()->items())
    {
        if (item && item->isVisible())
            ++visibleSceneItemCount;
    }
    passed &= check(
        view.visibleNodeCount() == 210 &&
        visibleSceneItemCount == 210,
        "node-kind filter hides disallowed nodes and every connecting edge");
    view.setSearchText(
        QStringLiteral(
            "unique-search-marker"));
    passed &= check(
        view.searchMatchCount() == 1 &&
        view.focusNextSearchMatch() &&
        view.executedNodeCount() == 2 &&
        view.executedEdgeCount() == 1,
        "search and filtering coexist with the execution overlay");

    const quint64 idleRebuildCount =
        view.traceOverlayRebuildCount();
    const quint64 idleWorkItemCount =
        view.traceOverlayWorkItemCount();
    view.setTraceOverlay(initialOverlay, 41);
    passed &= check(
        view.traceOverlayEpoch() == 1 &&
        view.traceOverlayRevision() == 1 &&
        view.traceOverlayRebuildCount() ==
            idleRebuildCount &&
        view.traceOverlayWorkItemCount() ==
            idleWorkItemCount,
        "an identical overlay revision performs zero presentation work");

    StoryGraphTraceGraphOverlay deltaOverlay =
        initialOverlay;
    deltaOverlay.revision = 2;
    deltaOverlay.executedNodeIds.insert(
        QStringLiteral("node-2"));
    deltaOverlay.executedEdgeIds.insert(
        QStringLiteral("edge-2"));
    StoryGraphTraceGraphOverlay::Delta delta;
    delta.revision = 2;
    delta.addedNodeIds.append(
        QStringLiteral("node-2"));
    delta.addedEdgeIds.append(
        QStringLiteral("edge-2"));
    deltaOverlay.deltas.append(delta);
    view.setTraceOverlay(deltaOverlay, 41);
    passed &= check(
        view.traceOverlayRevision() == 2 &&
        view.traceOverlayRebuildCount() ==
            idleRebuildCount &&
        view.traceOverlayWorkItemCount() -
                idleWorkItemCount ==
            2 &&
        view.executedNodeCount() == 3 &&
        view.executedEdgeCount() == 2,
        "one overlay delta touches only the newly executed node and edge");

    view.setAllowedNodeKinds({});
    const StoryGraphResult stale =
        makeGraph(600);
    view.setGraph(
        stale,
        makeLayout(stale),
        42);
    const StoryGraphResult latest =
        makeGraph(1);
    view.setGraph(
        latest,
        makeLayout(latest),
        43);
    passed &= check(
        waitForPresentation(view) &&
        view.generation() == 43 &&
        view.presentedNodeCount() == 1 &&
        view.executedNodeCount() == 0 &&
        view.executedEdgeCount() == 0,
        "a replacement generation invalidates every pending old batch");

    view.resetZoom();
    const qreal resetScale =
        view.transform().m11();
    view.zoomIn();
    const qreal zoomedScale =
        view.transform().m11();
    view.zoomOut();
    const qreal restoredScale =
        view.transform().m11();
    passed &= check(
        zoomedScale > resetScale &&
        qAbs(restoredScale - resetScale) <
            0.0001,
        "zoom controls update the transform and restore the prior scale");
    view.resetZoom();
    view.fitGraphInView();

    StoryGraphView cancelledView;
    const StoryGraphResult cancelledGraph =
        makeGraph(2000);
    int cancelledBatchCount = 0;
    int nodesAtCancellation = -1;
    QObject::connect(
        &cancelledView,
        &StoryGraphView::
            presentationBatchProcessed,
        [&cancelledView,
         &cancelledBatchCount,
         &nodesAtCancellation](int)
        {
            ++cancelledBatchCount;
            if (cancelledBatchCount == 1)
            {
                nodesAtCancellation =
                    cancelledView.
                        presentedNodeCount();
                cancelledView.
                    cancelPendingPresentation();
            }
        });
    cancelledView.setGraph(
        cancelledGraph,
        makeLayout(cancelledGraph),
        44);
    QElapsedTimer cancellationTimer;
    cancellationTimer.start();
    while (cancellationTimer.elapsed() < 250)
    {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents,
            20);
    }
    passed &= check(
        cancelledBatchCount == 1 &&
        nodesAtCancellation > 0 &&
        nodesAtCancellation <= 180 &&
        cancelledView.presentedNodeCount() ==
            nodesAtCancellation,
        "stale cancellation invalidates queued presentation before old nodes can grow");
    return passed;
}

bool testWindowStateAndResourceSwitchContract()
{
    StoryGraphWindow window;
    auto* modeCombo =
        window.findChild<QComboBox*>(
            QStringLiteral(
                "storyGraphModeCombo"));
    auto* nodeTypeCombo =
        window.findChild<QComboBox*>(
            QStringLiteral(
                "storyGraphNodeTypeFilter"));
    auto* searchEdit =
        window.findChild<QLineEdit*>(
            QStringLiteral(
                "storyGraphSearchEdit"));
    auto* warnings =
        window.findChild<QTreeWidget*>(
            QStringLiteral(
                "storyGraphWarningTree"));
    auto* status =
        window.findChild<QLabel*>(
            QStringLiteral(
                "storyGraphStatusLabel"));
    auto* staleStatus =
        window.findChild<QLabel*>(
            QStringLiteral(
                "storyGraphStaleLabel"));
    auto* zoomLabel =
        window.findChild<QLabel*>(
            QStringLiteral(
                "storyGraphZoomLabel"));
    auto* clearRuntimeTraceButton =
        window.findChild<QPushButton*>(
            QStringLiteral(
                "storyGraphClearRuntimeTraceButton"));
    auto* runtimeTraceStatus =
        window.findChild<QLabel*>(
            QStringLiteral(
                "storyGraphRuntimeTraceStatusLabel"));
    auto* runtimeTraceIssues =
        window.findChild<QTreeWidget*>(
            QStringLiteral(
                "storyGraphRuntimeTraceIssueTree"));
    auto* graphView =
        window.findChild<StoryGraphView*>();

    bool refreshRequested = false;
    QObject::connect(
        &window,
        &StoryGraphWindow::
            analysisRefreshRequested,
        [&refreshRequested]()
        {
            refreshRequested = true;
        });

    bool passed = true;
    passed &= check(
        modeCombo && modeCombo->count() == 2 &&
        nodeTypeCombo &&
        nodeTypeCombo->count() > 1 &&
        searchEdit && warnings && status &&
        staleStatus && zoomLabel && graphView &&
        clearRuntimeTraceButton &&
        runtimeTraceStatus &&
        runtimeTraceIssues,
        "story graph window exposes graph and runtime-trace controls");
    window.resize(930, 640);
    window.show();
    QCoreApplication::processEvents();
    passed &= check(
        modeCombo &&
        modeCombo->minimumWidth() >=
            modeCombo->sizeHint().width() &&
        modeCombo->width() >=
            modeCombo->minimumWidth() &&
        nodeTypeCombo &&
        nodeTypeCombo->minimumWidth() >=
        nodeTypeCombo->sizeHint().width() &&
        nodeTypeCombo->width() >=
            nodeTypeCombo->minimumWidth(),
        "graph mode and node type selectors keep their translated "
        "contents visible in a 930-pixel window");
    if (graphView && zoomLabel)
    {
        emit graphView->zoomFactorChanged(
            0.004321);
        passed &= check(
            zoomLabel->text() ==
                QStringLiteral("0.432%"),
            "positive sub-one-percent zoom remains nonzero "
            "and precise");
        emit graphView->zoomFactorChanged(1.0);
        passed &= check(
            zoomLabel->text() ==
                QStringLiteral("100%"),
            "whole zoom percentages remain compact");
        emit graphView->zoomFactorChanged(1.18);
        passed &= check(
            zoomLabel->text() ==
                QStringLiteral("118%"),
            "non-default whole zoom percentages remain "
            "compact");
        graphView->resetZoom();
        graphView->zoomIn();
        QEvent languageChange(
            QEvent::LanguageChange);
        QCoreApplication::sendEvent(
            &window,
            &languageChange);
        passed &= check(
            zoomLabel->text() ==
                QStringLiteral("118%"),
            "language changes preserve the actual zoom "
            "percentage");
    }
    window.showAnalysisError(
        QStringLiteral("fixture.error"),
        QStringLiteral("fixture message"));
    passed &= check(
        window.isStale() &&
        warnings->topLevelItemCount() == 1 &&
        staleStatus->text().contains(
            QStringLiteral("当前源码")),
        "analysis errors remain visible and explain that stale navigation uses current source");
    bool staleNavigationRequested = false;
    QObject::connect(
        &window,
        &StoryGraphWindow::
            sourceNavigationRequested,
        [&staleNavigationRequested](
            const StoryGraphNode&)
        {
            staleNavigationRequested = true;
        });
    if (graphView)
    {
        StoryGraphNode staleNode;
        emit graphView->nodeActivated(staleNode);
    }
    passed &= check(
        staleNavigationRequested,
        "stale StoryGraph nodes still request current-source navigation");
    window.showAnalysisError(
        QString(),
        QStringLiteral(
            "message-only fixture"));
    passed &= check(
        warnings && status &&
        warnings->topLevelItemCount() == 1 &&
        warnings->topLevelItem(0)->
            text(1).isEmpty() &&
        warnings->topLevelItem(0)->text(2) ==
            QStringLiteral(
                "message-only fixture") &&
        status->text().contains(
            QStringLiteral(
                "message-only fixture")),
        "a message-only analysis error remains visible in both warning and status text");

    refreshRequested = false;
    window.markStaleAndScheduleRefresh();
    const auto decision =
        window.prepareAssetsPathSwitch(
            QStringLiteral("D:/next assets"));
    QElapsedTimer refreshTimer;
    refreshTimer.start();
    while (!refreshRequested &&
           refreshTimer.elapsed() < 1000)
    {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents,
            20);
    }
    passed &= check(
        decision ==
            AssetsPathSwitchParticipant::
                Decision::Ready &&
        refreshRequested,
        "resource-switch prepare leaves derived graph refresh state intact");

    refreshRequested = false;
    passed &= check(
        window.resolveAssetsPathSwitch(
            decision),
        "derived graph state participates in resource switching without prompts");
    window.commitAssetsPathSwitch(
        QStringLiteral("D:/next assets"));
    passed &= check(
        refreshRequested &&
        window.currentAssetsPath() ==
            QStringLiteral("D:/next assets"),
        "resource switch commit updates the root and requests a fresh generation");
    return passed;
}

bool testScriptEditorImmutableStorySnapshot()
{
    QTemporaryDir temporaryDirectory;
    ScriptEditorWindow editor;
    const QString filePath =
        temporaryDirectory.filePath(
            QStringLiteral(
                "中文 script.txt"));
    const QByteArray firstBytes(
        "say(\"\xE4\xBD\xA0\xE5\xA5\xBD\")\n");
    bool passed = true;
    passed &= check(
        temporaryDirectory.isValid() &&
        editor.openVerifiedContent(
            filePath,
            firstBytes),
        "script editor opens verified UTF-8 content for snapshot tests");

    int changeCount = 0;
    QObject::connect(
        &editor,
        &ScriptEditorWindow::
            storyGraphSourceChanged,
        [&changeCount]()
        {
            ++changeCount;
        });
    const StoryGraphSourceSnapshot first =
        editor.storyGraphSourceSnapshot(
            QStringLiteral("active-root"),
            QStringLiteral(
                "script/map/中文/story.txt"));
    passed &= check(
        first.utf8Bytes == firstBytes &&
        first.identity.contentSha256 ==
            QCryptographicHash::hash(
                firstBytes,
                QCryptographicHash::Sha256) &&
        first.identity.documentRevision >= 0 &&
        first.identity.fromEditorBuffer,
        "story snapshot binds bytes, digest, revision, and buffer origin");

    auto* plainText =
        editor.findChild<ScriptEditor*>();
    if (plainText)
        plainText->appendPlainText(
            QStringLiteral("return"));
    const StoryGraphSourceSnapshot second =
        editor.storyGraphSourceSnapshot(
            QStringLiteral("active-root"),
            QStringLiteral(
                "script/map/中文/story.txt"));
    passed &= check(
        plainText &&
        changeCount > 0 &&
        second.identity.documentRevision !=
            first.identity.documentRevision &&
        second.identity.contentSha256 !=
            first.identity.contentSha256,
        "every buffer edit emits refresh input and changes snapshot identity");
    return passed;
}

bool testAnalysisErrorInvalidatesWorkerResult()
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create analysis-error race fixture root"))
    {
        return false;
    }

    const QString collectionRoot =
        temporaryDirectory.filePath(
            QStringLiteral(
                "资源 race collection"));
    bool passed = true;
    passed &= check(
        writeFile(
            QDir(collectionRoot).filePath(
                QStringLiteral("resources.ini")),
            QByteArray(
                "[Collection]\n"
                "CommonPath=common\n"
                "\n"
                "[Pack.MOD]\n"
                "Id=MOD\n"
                "Path=mod\n"
                "Manifest=game_profile.ini\n")) &&
        writeFile(
            QDir(collectionRoot).filePath(
                QStringLiteral(
                    "mod/game_profile.ini")),
            QByteArray(
                "[Game]\n"
                "Id=MOD\n"
                "Name=Graph Race Test\n"
                "Type=0\n")) &&
        QDir().mkpath(
            QDir(collectionRoot).filePath(
                QStringLiteral("common"))),
        "create exact resource context for analysis-error race");

    QString diagnosticCode;
    QString diagnosticMessage;
    const StoryGraphResourceContext context =
        StoryGraphResourceContext::resolve(
            collectionRoot,
            QStringLiteral("MOD"),
            1024 * 1024,
            &diagnosticCode,
            &diagnosticMessage);
    passed &= check(
        context.isValid(),
        "resolve analysis-error race resource context");
    if (!context.isValid())
        return false;

    StoryGraphWindow window;
    window.setResourceContext(context);
    const QList<StoryGraphContentRoot> roots =
        context.orderedContentRoots();
    const auto activeRoot = std::find_if(
        roots.cbegin(),
        roots.cend(),
        [](const StoryGraphContentRoot& root)
        {
            return root.kind ==
                StoryGraphContentRootKind::Active;
        });
    passed &= check(
        activeRoot != roots.cend(),
        "analysis-error race has an active root");
    if (activeRoot == roots.cend())
        return false;

    QByteArray source;
    source.reserve(60000);
    for (int index = 0; index < 3000; ++index)
        source.append("say(\"race\")\n");

    StoryGraphProjectRequest request;
    request.analysisGeneration = 700;
    request.entrySource.identity.portableRootKey =
        activeRoot->portableRootKey;
    request.entrySource.identity.virtualPath =
        QStringLiteral(
            "script/map/race/entry.txt");
    request.entrySource.utf8Bytes = source;
    request.orderedContentRoots = roots;
    request.entryMapContext.state =
        StoryGraphMapContextState::Assumed;
    request.entryMapContext.effectiveMapFolder =
        QStringLiteral("race");

    passed &= check(
        window.submitAnalysis(request),
        "analysis-error race starts a background generation");
    window.showAnalysisError(
        QStringLiteral("fixture.error"),
        QStringLiteral("fixture message"));

    QElapsedTimer timer;
    timer.start();
    while (window.isAnalyzing() &&
           timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents,
            20);
    }
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        50);
    auto* warnings =
        window.findChild<QTreeWidget*>(
            QStringLiteral(
                "storyGraphWarningTree"));
    passed &= check(
        !window.isAnalyzing() &&
        window.isStale() &&
        window.presentedGeneration() == 0 &&
        window.currentProjectResult() == nullptr &&
        warnings &&
        warnings->topLevelItemCount() == 1 &&
        warnings->topLevelItem(0)->text(1) ==
            QStringLiteral("fixture.error"),
        "an old worker result cannot replace a newer visible analysis error");
    return passed;
}

bool testWindowStaleStopsCurrentPresentation()
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create stale-presentation fixture root"))
    {
        return false;
    }

    const QString collectionRoot =
        temporaryDirectory.filePath(
            QStringLiteral(
                "资源 stale collection"));
    bool passed = true;
    passed &= check(
        writeFile(
            QDir(collectionRoot).filePath(
                QStringLiteral("resources.ini")),
            QByteArray(
                "[Collection]\n"
                "CommonPath=common\n"
                "\n"
                "[Pack.MOD]\n"
                "Id=MOD\n"
                "Path=mod\n"
                "Manifest=game_profile.ini\n")) &&
        writeFile(
            QDir(collectionRoot).filePath(
                QStringLiteral(
                    "mod/game_profile.ini")),
            QByteArray(
                "[Game]\n"
                "Id=MOD\n"
                "Name=Graph Stale Test\n"
                "Type=0\n")) &&
        QDir().mkpath(
            QDir(collectionRoot).filePath(
                QStringLiteral("common"))),
        "create exact resource context for stale presentation");

    QString diagnosticCode;
    QString diagnosticMessage;
    const StoryGraphResourceContext context =
        StoryGraphResourceContext::resolve(
            collectionRoot,
            QStringLiteral("MOD"),
            1024 * 1024,
            &diagnosticCode,
            &diagnosticMessage);
    passed &= check(
        context.isValid(),
        "resolve stale-presentation resource context");
    if (!context.isValid())
        return false;

    StoryGraphWindow window;
    window.setResourceContext(context);
    auto* view =
        window.findChild<StoryGraphView*>(
            QStringLiteral("storyGraphView"));
    const QList<StoryGraphContentRoot> roots =
        context.orderedContentRoots();
    const auto activeRoot = std::find_if(
        roots.cbegin(),
        roots.cend(),
        [](const StoryGraphContentRoot& root)
        {
            return root.kind ==
                StoryGraphContentRootKind::Active;
        });
    passed &= check(
        view && activeRoot != roots.cend(),
        "stale-presentation window exposes its view and active root");
    if (!view || activeRoot == roots.cend())
        return false;

    QByteArray source;
    source.reserve(24000);
    for (int index = 0; index < 1200; ++index)
        source.append("say(\"stale\")\n");

    StoryGraphProjectRequest request;
    request.analysisGeneration = 701;
    request.entrySource.identity.portableRootKey =
        activeRoot->portableRootKey;
    request.entrySource.identity.virtualPath =
        QStringLiteral(
            "script/map/stale/entry.txt");
    request.entrySource.utf8Bytes = source;
    request.orderedContentRoots = roots;
    request.entryMapContext.state =
        StoryGraphMapContextState::Assumed;
    request.entryMapContext.effectiveMapFolder =
        QStringLiteral("stale");

    int batchCount = 0;
    int nodesAtStale = -1;
    QObject::connect(
        view,
        &StoryGraphView::
            presentationBatchProcessed,
        [&window,
         view,
         &batchCount,
         &nodesAtStale](int)
        {
            ++batchCount;
            if (batchCount != 1)
                return;
            nodesAtStale =
                view->presentedNodeCount();
            window.markStaleAndScheduleRefresh();
        });
    passed &= check(
        window.submitAnalysis(request),
        "stale-presentation window starts a background generation");

    QElapsedTimer timer;
    timer.start();
    while (nodesAtStale < 0 &&
           timer.elapsed() < 10000)
    {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents,
            20);
    }
    const int stoppedNodeCount =
        view->presentedNodeCount();
    timer.restart();
    while (timer.elapsed() < 500)
    {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents,
            20);
    }
    const StoryGraphProjectResult* result =
        window.currentProjectResult();
    passed &= check(
        nodesAtStale > 0 &&
        nodesAtStale <= 180 &&
        stoppedNodeCount == nodesAtStale &&
        view->presentedNodeCount() ==
            nodesAtStale &&
        batchCount == 1 &&
        view->generation() == 701 &&
        window.presentedGeneration() == 701 &&
        window.isStale() &&
        result &&
        result->controlFlowGraph.nodes.size() >
            nodesAtStale,
        "window staleness cancels the current generation before any queued node batch can grow");
    return passed;
}

bool testMainWindowUsesOneGlobalGraphWindow()
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create MainWindow story graph fixture root"))
    {
        return false;
    }

    const QVariant previousConfigPath =
        qApp->property("configFilePath");
    qApp->setProperty(
        "configFilePath",
        temporaryDirectory.filePath(
            QStringLiteral("editor-test.ini")));

    const QString collectionRoot =
        temporaryDirectory.filePath(
            QStringLiteral("资源 collection"));
    const QString scriptPath =
        QDir(collectionRoot).filePath(
            QStringLiteral(
                "mod/script/map/测试地图/入口.txt"));
    const QString dependencyScriptPath =
        QDir(collectionRoot).filePath(
            QStringLiteral(
                "mod/script/map/测试地图/依赖.txt"));
    const QString projectPath =
        temporaryDirectory.filePath(
            QStringLiteral("graph-test.jxqyproj"));

    bool passed =
        writeFile(
            QDir(collectionRoot).filePath(
                QStringLiteral("resources.ini")),
            QByteArray(
                "[Collection]\n"
                "CommonPath=common\n"
                "\n"
                "[Pack.MOD]\n"
                "Id=MOD\n"
                "Path=mod\n"
                "Manifest=game_profile.ini\n")) &&
        writeFile(
            QDir(collectionRoot).filePath(
                QStringLiteral(
                    "mod/game_profile.ini")),
            QByteArray(
                "[Game]\n"
                "Id=MOD\n"
                "Name=Graph Test\n"
                "Type=0\n")) &&
        QDir().mkpath(
            QDir(collectionRoot).filePath(
                QStringLiteral("common"))) &&
        writeFile(
            scriptPath,
            QByteArray(
                "runscript(\"依赖.txt\")\n"
                "return\n")) &&
        writeFile(
            dependencyScriptPath,
            QByteArray("return\n"));
    passed &= check(
        passed,
        "create exact resource-selection fixture");

    {
        MainWindow mainWindow;
        ProjectResourceConfiguration configuration;
        configuration.editableAssetsRoot =
            collectionRoot;
        configuration.activeResourcePackId =
            QStringLiteral("MOD");
        passed &= check(
            mainWindow.createProject(
                projectPath,
                configuration) &&
                mainWindow.openStartupFileArguments(
                    {scriptPath}),
            "open a project and script through production MainWindow routes");

        auto* advancedDebugButton =
            mainWindow.findChild<QToolButton*>(
                QStringLiteral(
                    "scriptAdvancedDebugButton"));
        auto* graphAction =
            mainWindow.findChild<QAction*>(
                QStringLiteral(
                    "storyGraphCurrentScriptAction"));
        passed &= check(
            advancedDebugButton &&
                advancedDebugButton->menu() &&
                graphAction &&
                advancedDebugButton->menu()->actions().
                    contains(graphAction),
            "script advanced debug menu owns the story graph entry");
        if (graphAction)
            graphAction->trigger();

        QElapsedTimer timer;
        timer.start();
        StoryGraphWindow* graphWindow = nullptr;
        while (timer.elapsed() < 5000)
        {
            QCoreApplication::processEvents(
                QEventLoop::AllEvents,
                20);
            graphWindow =
                mainWindow.findChild<
                    StoryGraphWindow*>();
            if (graphWindow &&
                !graphWindow->isAnalyzing() &&
                graphWindow->currentProjectResult())
            {
                break;
            }
        }
        passed &= check(
            graphWindow &&
                !graphWindow->isStale() &&
                graphWindow->currentProjectResult() &&
                graphWindow->currentProjectResult()->
                    documents.size() == 2,
            "production entry publishes one current cross-file graph");

        if (graphAction)
            graphAction->trigger();
        QCoreApplication::processEvents(
            QEventLoop::AllEvents,
            20);
        passed &= check(
            mainWindow.findChildren<
                StoryGraphWindow*>().size() == 1,
            "reopening the story graph reuses one global window");
        if (graphWindow)
            graphWindow->clearRuntimeTraceSession();
    }

    ProjectManager& manager =
        ProjectManager::instance();
    if (manager.isProjectOpen())
        passed &= manager.closeProject();
    qApp->setProperty(
        "configFilePath",
        previousConfigPath);
    return passed;
}

bool testBoundedRuntimeTraceIssuePresentation()
{
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid())
    {
        return check(
            false,
            "create bounded runtime-trace presentation directory");
    }

    const QString sessionId =
        QStringLiteral(
            "77777777-7777-4777-8777-777777777777");
    const auto record =
        [&sessionId](
            quint64 sequence,
            const QString& eventType)
        {
            return QJsonObject{
                {
                    QStringLiteral("schemaVersion"),
                    1
                },
                {
                    QStringLiteral("sessionId"),
                    sessionId
                },
                {
                    QStringLiteral("sequence"),
                    static_cast<qint64>(sequence)
                },
                {
                    QStringLiteral("eventType"),
                    eventType
                }
            };
        };

    QByteArray bytes;
    bytes.reserve(160 * 1024);
    bytes += jsonLine(
        record(
            1,
            QStringLiteral("session.start")));
    constexpr quint64 DroppedEventCount = 700;
    for (quint64 sequence = 2;
         sequence <= DroppedEventCount + 1;
         ++sequence)
    {
        QJsonObject dropped =
            record(
                sequence,
                QStringLiteral("trace.dropped"));
        dropped.insert(
            QStringLiteral(
                "droppedSourceLineCount"),
            1);
        bytes += jsonLine(dropped);
    }
    QJsonObject finish =
        record(
            DroppedEventCount + 2,
            QStringLiteral("session.finish"));
    finish.insert(
        QStringLiteral("status"),
        QStringLiteral("completed"));
    bytes += jsonLine(finish);

    const QString path =
        temporaryDirectory.filePath(
            QStringLiteral(
                "bounded runtime trace.jsonl"));
    bool passed = true;
    passed &= check(
        writeFile(path, bytes),
        "write a large runtime-trace issue fixture");

    StoryGraphWindow window;
    const quint64 workBefore =
        window.
            runtimeTraceIssuePresentationWorkItemCount();
    window.bindRuntimeTraceSession(
        sessionId,
        path,
        false);
    auto* issues =
        window.findChild<QTreeWidget*>(
            QStringLiteral(
                "storyGraphRuntimeTraceIssueTree"));
    const StoryGraphTraceGraphIssueSummary&
        summary =
            window.runtimeTraceMatchResult().
                storySemanticsIssues;
    const quint64 presentationWork =
        window.
            runtimeTraceIssuePresentationWorkItemCount() -
        workBefore;
    passed &= check(
        window.runtimeTraceEventCount() ==
                static_cast<qsizetype>(
                    DroppedEventCount + 2) &&
            summary.issueCount ==
                static_cast<qsizetype>(
                    DroppedEventCount) &&
            summary.presentedIssues.size() ==
                StoryGraphTraceGraphIssueSummary::
                    MaximumPresentedIssueCount &&
            issues &&
            issues->topLevelItemCount() ==
                StoryGraphTraceGraphIssueSummary::
                    MaximumPresentedIssueCount &&
            presentationWork <=
                static_cast<quint64>(
                    StoryGraphTraceGraphIssueSummary::
                        MaximumPresentedIssueCount),
        "a large trace history presents and scans at most 500 cached issue rows");
    passed &= check(
        issues &&
            issues->topLevelItemCount() > 0 &&
            issues->topLevelItem(0)->
                text(2).contains(
                    QStringLiteral("源码行")),
        "source.line issue details use the translatable desktop UI wording");
    return passed;
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    bool passed = true;
    passed &= testBatchedPresentationAndReplacement();
    passed &= testWindowStateAndResourceSwitchContract();
    passed &= testScriptEditorImmutableStorySnapshot();
    passed &= testAnalysisErrorInvalidatesWorkerResult();
    passed &= testWindowStaleStopsCurrentPresentation();
    passed &= testMainWindowUsesOneGlobalGraphWindow();
    passed &= testBoundedRuntimeTraceIssuePresentation();
    return passed ? 0 : 1;
}
