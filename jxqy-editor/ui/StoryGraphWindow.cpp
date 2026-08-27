#include "StoryGraphWindow.h"

#include "DesktopRunPanel.h"
#include "StoryGraphView.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <utility>

namespace
{
constexpr int MaximumPresentedWarningRows = 500;
constexpr int MaximumPresentedRuntimeTraceRows = 500;
constexpr int RuntimeTraceRefreshIntervalMilliseconds = 125;

void preserveComboContentWidth(QComboBox* comboBox)
{
    comboBox->setSizeAdjustPolicy(
        QComboBox::AdjustToContents);
    comboBox->setSizePolicy(
        QSizePolicy::Minimum,
        QSizePolicy::Fixed);
    comboBox->setMinimumWidth(0);
    comboBox->setMinimumWidth(
        comboBox->sizeHint().width());
}

QString zoomPercentageText(qreal factor)
{
    const qreal percentage = factor * 100.0;
    if (percentage > 0.0 && percentage < 1.0)
    {
        return QStringLiteral("%1%").arg(
            QString::number(
                percentage,
                'g',
                3));
    }
    return QStringLiteral("%1%").arg(
        qRound(percentage));
}

struct NodeKindOption
{
    StoryGraphNodeKind kind;
    const char* sourceText;
};

constexpr std::array<NodeKindOption, 27>
NodeKindOptions = {{
    {StoryGraphNodeKind::ChunkEntry, QT_TRANSLATE_NOOP("StoryGraphWindow", "脚本入口")},
    {StoryGraphNodeKind::ChunkExit, QT_TRANSLATE_NOOP("StoryGraphWindow", "脚本出口")},
    {StoryGraphNodeKind::FunctionEntry, QT_TRANSLATE_NOOP("StoryGraphWindow", "函数入口")},
    {StoryGraphNodeKind::FunctionExit, QT_TRANSLATE_NOOP("StoryGraphWindow", "函数出口")},
    {StoryGraphNodeKind::BasicBlock, QT_TRANSLATE_NOOP("StoryGraphWindow", "基本块")},
    {StoryGraphNodeKind::Statement, QT_TRANSLATE_NOOP("StoryGraphWindow", "语句")},
    {StoryGraphNodeKind::Condition, QT_TRANSLATE_NOOP("StoryGraphWindow", "条件")},
    {StoryGraphNodeKind::Merge, QT_TRANSLATE_NOOP("StoryGraphWindow", "合流")},
    {StoryGraphNodeKind::LoopHeader, QT_TRANSLATE_NOOP("StoryGraphWindow", "循环")},
    {StoryGraphNodeKind::Label, QT_TRANSLATE_NOOP("StoryGraphWindow", "标签")},
    {StoryGraphNodeKind::Goto, QT_TRANSLATE_NOOP("StoryGraphWindow", "跳转")},
    {StoryGraphNodeKind::Return, QT_TRANSLATE_NOOP("StoryGraphWindow", "返回")},
    {StoryGraphNodeKind::Break, QT_TRANSLATE_NOOP("StoryGraphWindow", "中断循环")},
    {StoryGraphNodeKind::Unreachable, QT_TRANSLATE_NOOP("StoryGraphWindow", "不可达")},
    {StoryGraphNodeKind::Dialogue, QT_TRANSLATE_NOOP("StoryGraphWindow", "对话")},
    {StoryGraphNodeKind::Choice, QT_TRANSLATE_NOOP("StoryGraphWindow", "选择")},
    {StoryGraphNodeKind::VariableRead, QT_TRANSLATE_NOOP("StoryGraphWindow", "变量读取")},
    {StoryGraphNodeKind::VariableWrite, QT_TRANSLATE_NOOP("StoryGraphWindow", "变量写入")},
    {StoryGraphNodeKind::SerialScriptCall, QT_TRANSLATE_NOOP("StoryGraphWindow", "同步脚本调用")},
    {StoryGraphNodeKind::ParallelScriptCall, QT_TRANSLATE_NOOP("StoryGraphWindow", "并行脚本调用")},
    {StoryGraphNodeKind::MapLoad, QT_TRANSLATE_NOOP("StoryGraphWindow", "地图切换")},
    {StoryGraphNodeKind::Battle, QT_TRANSLATE_NOOP("StoryGraphWindow", "战斗")},
    {StoryGraphNodeKind::RegisteredApiCall, QT_TRANSLATE_NOOP("StoryGraphWindow", "运行时 API")},
    {StoryGraphNodeKind::DynamicCall, QT_TRANSLATE_NOOP("StoryGraphWindow", "动态调用")},
    {StoryGraphNodeKind::UnknownCall, QT_TRANSLATE_NOOP("StoryGraphWindow", "未知调用")},
    {StoryGraphNodeKind::MissingTarget, QT_TRANSLATE_NOOP("StoryGraphWindow", "缺失目标")},
    {StoryGraphNodeKind::Warning, QT_TRANSLATE_NOOP("StoryGraphWindow", "警告")}
}};

QString sourceLocationText(
    const StoryGraphWarning& warning)
{
    if (warning.source.virtualPath.isEmpty())
        return {};
    if (!warning.sourceRange.isValid())
        return warning.source.virtualPath;
    return QStringLiteral("%1:%2:%3")
        .arg(warning.source.virtualPath)
        .arg(warning.sourceRange.start.line)
        .arg(warning.sourceRange.start.column);
}

QString projectStatusText(
    StoryGraphProjectStatus status)
{
    switch (status)
    {
    case StoryGraphProjectStatus::Complete:
        return QObject::tr("分析完成");
    case StoryGraphProjectStatus::Partial:
        return QObject::tr("分析部分完成");
    case StoryGraphProjectStatus::Failed:
        return QObject::tr("分析失败");
    case StoryGraphProjectStatus::Cancelled:
        return QObject::tr("分析已取消");
    }
    return QObject::tr("分析状态未知");
}

QString runtimeTraceEventDetail(
    const StoryGraphRuntimeTraceEvent& event)
{
    switch (event.type)
    {
    case StoryGraphRuntimeTraceEventType::ScriptStart:
        return event.virtualPath;
    case StoryGraphRuntimeTraceEventType::ScriptFinish:
        return event.status;
    case StoryGraphRuntimeTraceEventType::SourceLine:
        return QCoreApplication::translate(
                   "StoryGraphWindow",
                   "源码行 %1")
            .arg(event.line);
    case StoryGraphRuntimeTraceEventType::ApiCall:
        return event.apiName;
    case StoryGraphRuntimeTraceEventType::MapChange:
        return event.target;
    case StoryGraphRuntimeTraceEventType::VariableChange:
        return event.variableName;
    case StoryGraphRuntimeTraceEventType::TraceDropped:
        return QCoreApplication::translate(
                   "StoryGraphWindow",
                   "%1 条源码行事件被丢弃")
            .arg(event.droppedSourceLineCount);
    case StoryGraphRuntimeTraceEventType::SessionStart:
    case StoryGraphRuntimeTraceEventType::SessionFinish:
        return event.status;
    }
    return {};
}

}

StoryGraphWindow::StoryGraphWindow(QWidget* parent)
    : QWidget(parent)
    , coordinator(
          new StoryGraphAnalysisCoordinator(this))
{
    setObjectName(QStringLiteral("storyGraphWindow"));
    setAttribute(Qt::WA_DeleteOnClose);
    setupUi();

    connect(
        coordinator,
        &StoryGraphAnalysisCoordinator::analysisCompleted,
        this,
        [this](const StoryGraphAnalysisBundle& bundle)
        {
            if (requestedGeneration == 0 ||
                bundle.projectResult.analysisGeneration !=
                requestedGeneration)
            {
                return;
            }
            currentBundle = bundle;
            currentPresentedGeneration =
                bundle.projectResult.analysisGeneration;
            lastDiagnosticCode.clear();
            lastDiagnosticMessage.clear();
            setStale(false);
            rematchRuntimeTrace();
            presentCurrentGraph();
            presentWarnings();
            updateStatusText();
        });
    connect(
        coordinator,
        &StoryGraphAnalysisCoordinator::busyChanged,
        this,
        [this](bool)
        {
            updateStatusText();
        });
}

StoryGraphWindow::~StoryGraphWindow()
{
    runtimeTraceRefreshTimer->stop();
    coordinator->shutdown();
}

void StoryGraphWindow::setResourceContext(
    const StoryGraphResourceContext& context)
{
    currentResourceContext = context;
    assetsPath =
        context.assetsCollectionRoot();
}

StoryGraphResourceContext
StoryGraphWindow::resourceContext() const
{
    return currentResourceContext;
}

bool StoryGraphWindow::submitAnalysis(
    const StoryGraphProjectRequest& request)
{
    if (!currentResourceContext.isValid())
    {
        showAnalysisError(
            QStringLiteral(
                "story_graph.resource.context_unavailable"),
            tr("剧情图资源上下文不可用。"));
        return false;
    }
    if (request.entrySource.identity.virtualPath.isEmpty() ||
        request.entrySource.identity.portableRootKey.isEmpty())
    {
        showAnalysisError(
            QStringLiteral(
                "story_graph.entry_source_invalid"),
            tr("剧情图入口脚本没有稳定的活动资源路径。"));
        return false;
    }

    analysisRefreshTimer->stop();
    requestedGeneration =
        request.analysisGeneration;
    currentBundle.reset();
    currentPresentedGeneration = 0;
    lastDiagnosticCode.clear();
    lastDiagnosticMessage.clear();
    setStale(true);
    graphView->clearGraph();
    warningTree->clear();

    const StoryGraphResourceContext context =
        currentResourceContext;
    const bool accepted =
        coordinator->submit(
            request,
            [context](
                const StoryGraphContentRoot& root,
                const QString& virtualPath)
            {
                return context.read(root, virtualPath);
            },
            [context](const QString& mapTarget)
            {
                return context.resolveMapFolder(
                    mapTarget);
            });
    if (!accepted)
    {
        showAnalysisError(
            QStringLiteral(
                "story_graph.analysis.request_rejected"),
            tr("剧情图后台分析请求未被接受。"));
        return false;
    }
    updateStatusText();
    return true;
}

void StoryGraphWindow::showAnalysisError(
    const QString& diagnosticCode,
    const QString& message)
{
    analysisRefreshTimer->stop();
    coordinator->cancel();
    requestedGeneration = 0;
    currentBundle.reset();
    currentPresentedGeneration = 0;
    lastDiagnosticCode = diagnosticCode;
    lastDiagnosticMessage = message;
    setStale(true);
    graphView->clearGraph();
    warningTree->clear();
    if (!diagnosticCode.isEmpty() ||
        !message.isEmpty())
    {
        auto* item = new QTreeWidgetItem(
            warningTree);
        item->setText(0, tr("错误"));
        item->setText(1, diagnosticCode);
        item->setText(2, message);
    }
    updateStatusText();
}

void StoryGraphWindow::markStaleAndScheduleRefresh()
{
    if (!stale)
        setStale(true);
    requestedGeneration = 0;
    coordinator->cancel();
    graphView->cancelPendingPresentation();
    analysisRefreshTimer->start();
    updateStatusText();
}

void StoryGraphWindow::cancelAnalysis()
{
    analysisRefreshTimer->stop();
    requestedGeneration = 0;
    coordinator->cancel();
    graphView->cancelPendingPresentation();
    setStale(true);
    updateStatusText();
}

bool StoryGraphWindow::isStale() const
{
    return stale;
}

bool StoryGraphWindow::isAnalyzing() const
{
    return coordinator->isBusy();
}

quint64 StoryGraphWindow::presentedGeneration() const
{
    return currentPresentedGeneration;
}

StoryGraphKind StoryGraphWindow::displayedGraphKind() const
{
    return selectedGraphKind();
}

const StoryGraphProjectResult*
StoryGraphWindow::currentProjectResult() const
{
    return currentBundle
        ? &currentBundle->projectResult
        : nullptr;
}

void StoryGraphWindow::bindRuntimeTraceSession(
    const QString& sessionId,
    const QString& runtimeTracePath,
    bool producerActive,
    bool forcedTermination)
{
    if (sessionId.isEmpty() ||
        runtimeTracePath.isEmpty())
    {
        clearRuntimeTraceSession();
        return;
    }

    const bool sameBinding =
        sessionId == currentRuntimeTraceSessionId &&
        runtimeTracePath == currentRuntimeTracePath;
    if (sameBinding)
    {
        if (producerActive)
        {
            if (!runtimeTraceProducerTerminal &&
                !runtimeTraceMatcherFailed)
                runtimeTraceRefreshTimer->start();
        }
        else if (!runtimeTraceProducerTerminal)
        {
            finalizeRuntimeTraceSession(
                sessionId,
                forcedTermination);
        }
        return;
    }

    runtimeTraceRefreshTimer->stop();
    currentRuntimeTraceSessionId = sessionId;
    currentRuntimeTracePath = runtimeTracePath;
    appliedRuntimeTraceEventCount = 0;
    runtimeTraceProducerTerminal =
        !producerActive;
    runtimeTraceForcedTermination =
        forcedTermination;
    runtimeTraceFinalizationIssued = false;
    runtimeTraceMatcherFailed = false;
    runtimeTraceTailer.bindSource(
        runtimeTracePath,
        sessionId);
    runtimeTraceMatcher.resetTrace(sessionId);
    if (currentBundle && !stale)
    {
        runtimeTraceMatcher.setProjectResult(
            currentBundle->projectResult);
    }
    graphView->clearTraceOverlay();
    presentRuntimeTraceIssues();
    updateRuntimeTraceStatusText();
    refreshRuntimeTrace();
    if (!runtimeTraceMatcherFailed &&
        (producerActive ||
         runtimeTraceTailer.hasUnreadBytes()))
    {
        runtimeTraceRefreshTimer->start();
    }
}

void StoryGraphWindow::finalizeRuntimeTraceSession(
    const QString& sessionId,
    bool forcedTermination)
{
    if (sessionId.isEmpty() ||
        sessionId != currentRuntimeTraceSessionId)
    {
        return;
    }
    runtimeTraceProducerTerminal = true;
    runtimeTraceForcedTermination =
        forcedTermination;
    runtimeTraceFinalizationIssued = false;
    refreshRuntimeTrace();
    if (!runtimeTraceMatcherFailed &&
        runtimeTraceTailer.hasUnreadBytes())
        runtimeTraceRefreshTimer->start();
}

void StoryGraphWindow::clearRuntimeTraceSession()
{
    runtimeTraceRefreshTimer->stop();
    runtimeTraceTailer.clear();
    runtimeTraceMatcher.clearProjectResult();
    runtimeTraceMatcher.resetTrace(QString());
    currentRuntimeTraceSessionId.clear();
    currentRuntimeTracePath.clear();
    appliedRuntimeTraceEventCount = 0;
    runtimeTraceProducerTerminal = false;
    runtimeTraceForcedTermination = false;
    runtimeTraceFinalizationIssued = false;
    runtimeTraceMatcherFailed = false;
    graphView->clearTraceOverlay();
    runtimeTraceIssueTree->clear();
    updateRuntimeTraceStatusText();
}

void StoryGraphWindow::clearRuntimeTraceOverlayMemory()
{
    if (currentRuntimeTraceSessionId.isEmpty())
        return;

    const qsizetype eventCount =
        runtimeTraceTailer.eventCount();
    if (appliedRuntimeTraceEventCount <
            eventCount &&
        !appendRuntimeTraceEvents(
            appliedRuntimeTraceEventCount,
            eventCount))
    {
        presentRuntimeTraceIssues();
        updateRuntimeTraceStatusText();
        return;
    }
    const quint64 tailerSequence =
        runtimeTraceTailer.
            discardEventsRetainingCursor();
    const quint64 matcherSequence =
        runtimeTraceMatcher.
            discardMatchesRetainingExecutionState();
    if (matcherSequence != tailerSequence)
    {
        failRuntimeTraceMatching();
        presentRuntimeTraceIssues();
        updateRuntimeTraceStatusText();
        return;
    }

    appliedRuntimeTraceEventCount = 0;
    graphView->clearTraceOverlay();
    presentRuntimeTraceIssues();
    updateRuntimeTraceStatusText();
}

QString StoryGraphWindow::
selectedRuntimeTraceSessionId() const
{
    return currentRuntimeTraceSessionId;
}

StoryGraphRuntimeTraceStreamState
StoryGraphWindow::runtimeTraceStreamState() const
{
    return runtimeTraceTailer.state();
}

qsizetype StoryGraphWindow::
runtimeTraceEventCount() const
{
    return runtimeTraceTailer.eventCount();
}

const StoryGraphTraceMatchResult&
StoryGraphWindow::runtimeTraceMatchResult() const
{
    return runtimeTraceMatcher.result();
}

quint64 StoryGraphWindow::
runtimeTraceIssuePresentationRevision() const
{
    return currentRuntimeTraceIssuePresentationRevision;
}

quint64 StoryGraphWindow::
runtimeTraceIssuePresentationRebuildCount() const
{
    return currentRuntimeTraceIssuePresentationRebuildCount;
}

quint64 StoryGraphWindow::
runtimeTraceIssuePresentationWorkItemCount() const
{
    return currentRuntimeTraceIssuePresentationWorkItemCount;
}

bool StoryGraphWindow::readCurrentDiskSource(
    const StoryGraphSourceIdentity& source,
    QString& currentAbsolutePath,
    QByteArray& currentBytes,
    bool& matchesAnalyzedContent) const
{
    currentAbsolutePath.clear();
    currentBytes.clear();
    matchesAnalyzedContent = false;
    if (!currentResourceContext.isValid() ||
        source.portableRootKey.isEmpty() ||
        source.virtualPath.isEmpty())
    {
        return false;
    }

    const QList<StoryGraphContentRoot> roots =
        currentResourceContext.orderedContentRoots();
    const auto root = std::find_if(
        roots.cbegin(),
        roots.cend(),
        [&source](const StoryGraphContentRoot& candidate)
        {
            return candidate.portableRootKey ==
                source.portableRootKey;
        });
    if (root == roots.cend())
        return false;

    const StoryGraphReadResult read =
        currentResourceContext.read(
            *root,
            source.virtualPath);
    if (read.status != StoryGraphReadStatus::Found ||
        read.canonicalAbsolutePath.isEmpty())
    {
        return false;
    }

    currentAbsolutePath =
        read.canonicalAbsolutePath;
    currentBytes = read.utf8Bytes;
    matchesAnalyzedContent =
        source.contentSha256.isEmpty() ||
        QCryptographicHash::hash(
            currentBytes,
            QCryptographicHash::Sha256) ==
            source.contentSha256;
    return true;
}

AssetsPathSwitchParticipant::PathScope
StoryGraphWindow::assetsPathScope() const
{
    return PathScope::ResourceCollectionRoot;
}

AssetsPathSwitchParticipant::Decision
StoryGraphWindow::prepareAssetsPathSwitch(
    const QString&) const
{
    return Decision::Ready;
}

bool StoryGraphWindow::resolveAssetsPathSwitch(
    Decision decision)
{
    if (decision == Decision::Cancelled)
        return false;
    analysisRefreshTimer->stop();
    requestedGeneration = 0;
    // The active worker owns value copies of its resource context. Cancel its
    // generation and let it finish independently so a slow current-path read
    // cannot freeze a resource-root switch on the GUI thread.
    coordinator->cancel();
    currentResourceContext.clear();
    currentBundle.reset();
    currentPresentedGeneration = 0;
    graphView->clearGraph();
    warningTree->clear();
    clearRuntimeTraceSession();
    setStale(true);
    return true;
}

void StoryGraphWindow::commitAssetsPathSwitch(
    const QString& path)
{
    assetsPath = path;
    emit analysisRefreshRequested();
}

QString StoryGraphWindow::currentAssetsPath() const
{
    return assetsPath;
}

void StoryGraphWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        retranslateUi();
        if (currentBundle || coordinator->isBusy())
        {
            // Core warnings and generic node titles are translated while the
            // immutable result is built. Regenerate them under the newly
            // installed translator instead of mixing languages in one view.
            markStaleAndScheduleRefresh();
        }
    }
    QWidget::changeEvent(event);
}

void StoryGraphWindow::closeEvent(QCloseEvent* event)
{
    analysisRefreshTimer->stop();
    runtimeTraceRefreshTimer->stop();
    runtimeTraceTailer.clear();
    coordinator->shutdown();
    currentResourceContext.clear();
    emit graphWindowClosed();
    event->accept();
}

void StoryGraphWindow::setupUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    auto* toolbar = new QHBoxLayout();

    graphModeCombo = new QComboBox(this);
    graphModeCombo->setObjectName(
        QStringLiteral("storyGraphModeCombo"));
    nodeTypeFilterCombo = new QComboBox(this);
    nodeTypeFilterCombo->setObjectName(
        QStringLiteral("storyGraphNodeTypeFilter"));
    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName(
        QStringLiteral("storyGraphSearchEdit"));
    searchEdit->setClearButtonEnabled(true);
    searchNextButton = new QToolButton(this);
    searchNextButton->setObjectName(
        QStringLiteral("storyGraphSearchNextButton"));
    zoomOutButton = new QToolButton(this);
    zoomOutButton->setObjectName(
        QStringLiteral("storyGraphZoomOutButton"));
    zoomInButton = new QToolButton(this);
    zoomInButton->setObjectName(
        QStringLiteral("storyGraphZoomInButton"));
    resetZoomButton = new QToolButton(this);
    resetZoomButton->setObjectName(
        QStringLiteral("storyGraphResetZoomButton"));
    fitButton = new QToolButton(this);
    fitButton->setObjectName(
        QStringLiteral("storyGraphFitButton"));
    refreshButton = new QPushButton(this);
    refreshButton->setObjectName(
        QStringLiteral("storyGraphRefreshButton"));
    zoomLabel = new QLabel(this);
    zoomLabel->setObjectName(
        QStringLiteral("storyGraphZoomLabel"));

    toolbar->addWidget(graphModeCombo);
    toolbar->addWidget(nodeTypeFilterCombo);
    toolbar->addWidget(searchEdit, 1);
    toolbar->addWidget(searchNextButton);
    toolbar->addSpacing(8);
    toolbar->addWidget(zoomOutButton);
    toolbar->addWidget(zoomInButton);
    toolbar->addWidget(resetZoomButton);
    toolbar->addWidget(fitButton);
    toolbar->addWidget(zoomLabel);
    toolbar->addSpacing(8);
    toolbar->addWidget(refreshButton);
    rootLayout->addLayout(toolbar);

    auto* runtimeTraceToolbar =
        new QHBoxLayout();
    clearRuntimeTraceButton =
        new QPushButton(this);
    clearRuntimeTraceButton->setObjectName(
        QStringLiteral(
            "storyGraphClearRuntimeTraceButton"));
    runtimeTraceStatusLabel =
        new QLabel(this);
    runtimeTraceStatusLabel->setObjectName(
        QStringLiteral(
            "storyGraphRuntimeTraceStatusLabel"));
    runtimeTraceStatusLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    runtimeTraceToolbar->addWidget(
        clearRuntimeTraceButton);
    runtimeTraceToolbar->addSpacing(8);
    runtimeTraceToolbar->addWidget(
        runtimeTraceStatusLabel,
        1);
    rootLayout->addLayout(runtimeTraceToolbar);

    auto* splitter =
        new QSplitter(Qt::Vertical, this);
    splitter->setObjectName(
        QStringLiteral("storyGraphSplitter"));
    graphView = new StoryGraphView(splitter);
    warningTree = new QTreeWidget(splitter);
    warningTree->setObjectName(
        QStringLiteral("storyGraphWarningTree"));
    warningTree->setRootIsDecorated(false);
    warningTree->setAlternatingRowColors(true);
    warningTree->header()->setStretchLastSection(true);
    warningTree->setMinimumHeight(110);
    runtimeTraceIssueTree =
        new QTreeWidget(splitter);
    runtimeTraceIssueTree->setObjectName(
        QStringLiteral(
            "storyGraphRuntimeTraceIssueTree"));
    runtimeTraceIssueTree->setRootIsDecorated(false);
    runtimeTraceIssueTree->
        setAlternatingRowColors(true);
    runtimeTraceIssueTree->header()->
        setStretchLastSection(true);
    runtimeTraceIssueTree->setMinimumHeight(100);
    splitter->addWidget(graphView);
    splitter->addWidget(warningTree);
    splitter->addWidget(runtimeTraceIssueTree);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setStretchFactor(2, 0);
    rootLayout->addWidget(splitter, 1);

    auto* statusLayout = new QHBoxLayout();
    statusLabel = new QLabel(this);
    statusLabel->setObjectName(
        QStringLiteral("storyGraphStatusLabel"));
    staleLabel = new QLabel(this);
    staleLabel->setObjectName(
        QStringLiteral("storyGraphStaleLabel"));
    statusLayout->addWidget(statusLabel, 1);
    statusLayout->addWidget(staleLabel);
    rootLayout->addLayout(statusLayout);

    searchDebounceTimer = new QTimer(this);
    searchDebounceTimer->setSingleShot(true);
    searchDebounceTimer->setInterval(125);
    analysisRefreshTimer = new QTimer(this);
    analysisRefreshTimer->setSingleShot(true);
    analysisRefreshTimer->setInterval(350);
    runtimeTraceRefreshTimer = new QTimer(this);
    runtimeTraceRefreshTimer->setInterval(
        RuntimeTraceRefreshIntervalMilliseconds);

    populateGraphModeCombo();
    populateNodeTypeFilter();
    retranslateUi();

    connect(
        graphModeCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int)
        {
            presentCurrentGraph();
            presentWarnings();
            applyRuntimeTraceOverlay();
            presentRuntimeTraceIssues();
        });
    connect(
        nodeTypeFilterCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this](int index)
        {
            QSet<StoryGraphNodeKind> kinds;
            if (index > 0)
            {
                kinds.insert(
                    static_cast<StoryGraphNodeKind>(
                        nodeTypeFilterCombo->
                            itemData(index).toInt()));
            }
            graphView->setAllowedNodeKinds(kinds);
        });
    connect(
        searchEdit,
        &QLineEdit::textChanged,
        searchDebounceTimer,
        qOverload<>(&QTimer::start));
    connect(
        searchDebounceTimer,
        &QTimer::timeout,
        this,
        [this]()
        {
            graphView->setSearchText(
                searchEdit->text());
        });
    connect(
        searchNextButton,
        &QToolButton::clicked,
        graphView,
        &StoryGraphView::focusNextSearchMatch);
    connect(
        zoomInButton,
        &QToolButton::clicked,
        graphView,
        &StoryGraphView::zoomIn);
    connect(
        zoomOutButton,
        &QToolButton::clicked,
        graphView,
        &StoryGraphView::zoomOut);
    connect(
        resetZoomButton,
        &QToolButton::clicked,
        graphView,
        &StoryGraphView::resetZoom);
    connect(
        fitButton,
        &QToolButton::clicked,
        graphView,
        &StoryGraphView::fitGraphInView);
    connect(
        refreshButton,
        &QPushButton::clicked,
        this,
        &StoryGraphWindow::analysisRefreshRequested);
    connect(
        clearRuntimeTraceButton,
        &QPushButton::clicked,
        this,
        &StoryGraphWindow::
            clearRuntimeTraceOverlayMemory);
    connect(
        analysisRefreshTimer,
        &QTimer::timeout,
        this,
        &StoryGraphWindow::analysisRefreshRequested);
    connect(
        runtimeTraceRefreshTimer,
        &QTimer::timeout,
        this,
        &StoryGraphWindow::refreshRuntimeTrace);
    connect(
        graphView,
        &StoryGraphView::nodeActivated,
        this,
        [this](const StoryGraphNode& node)
        {
            emit sourceNavigationRequested(node);
        });
    connect(
        graphView,
        &StoryGraphView::zoomFactorChanged,
        this,
        [this](qreal factor)
        {
            zoomLabel->setText(
                zoomPercentageText(factor));
        });
    connect(
        graphView,
        &StoryGraphView::visibleNodeCountChanged,
        this,
        [this](int, int)
        {
            updateStatusText();
        });
}

void StoryGraphWindow::populateGraphModeCombo()
{
    graphModeCombo->clear();
    graphModeCombo->addItem(
        tr("控制流"),
        static_cast<int>(
            StoryGraphKind::ControlFlow));
    graphModeCombo->addItem(
        tr("剧情语义"),
        static_cast<int>(
            StoryGraphKind::StorySemantics));
    preserveComboContentWidth(
        graphModeCombo);
}

void StoryGraphWindow::populateNodeTypeFilter()
{
    const int previousKind =
        nodeTypeFilterCombo->currentIndex() > 0
        ? nodeTypeFilterCombo->currentData().toInt()
        : -1;
    nodeTypeFilterCombo->clear();
    nodeTypeFilterCombo->addItem(
        tr("全部节点"), -1);
    int restoredIndex = 0;
    for (const NodeKindOption& option :
         NodeKindOptions)
    {
        nodeTypeFilterCombo->addItem(
            tr(option.sourceText),
            static_cast<int>(option.kind));
        if (static_cast<int>(option.kind) ==
            previousKind)
        {
            restoredIndex =
                nodeTypeFilterCombo->count() - 1;
        }
    }
    nodeTypeFilterCombo->setCurrentIndex(
        restoredIndex);
    preserveComboContentWidth(
        nodeTypeFilterCombo);
}

void StoryGraphWindow::retranslateUi()
{
    setWindowTitle(tr("剧情图"));
    const StoryGraphKind previousMode =
        selectedGraphKind();
    populateGraphModeCombo();
    const int targetMode =
        graphModeCombo->findData(
            static_cast<int>(previousMode));
    if (targetMode >= 0)
        graphModeCombo->setCurrentIndex(targetMode);
    populateNodeTypeFilter();

    searchEdit->setPlaceholderText(
        tr("搜索节点、API、变量或路径"));
    searchNextButton->setText(tr("下一个"));
    searchNextButton->setToolTip(
        tr("定位下一个搜索结果"));
    zoomOutButton->setText(QStringLiteral("−"));
    zoomOutButton->setToolTip(tr("缩小"));
    zoomInButton->setText(QStringLiteral("+"));
    zoomInButton->setToolTip(tr("放大"));
    resetZoomButton->setText(tr("100%"));
    resetZoomButton->setToolTip(tr("恢复 100% 缩放"));
    fitButton->setText(tr("适配"));
    fitButton->setToolTip(tr("适配当前可见节点"));
    refreshButton->setText(tr("刷新"));
    clearRuntimeTraceButton->setText(
        tr("清除叠加"));
    clearRuntimeTraceButton->setToolTip(
        tr("只清除当前运行在编辑器内存中的轨迹叠加"));
    zoomLabel->setText(
        zoomPercentageText(
            graphView->zoomFactor()));
    warningTree->setHeaderLabels({
        tr("级别"),
        tr("诊断码"),
        tr("说明"),
        tr("来源")
    });
    runtimeTraceIssueTree->setHeaderLabels({
        tr("匹配状态"),
        tr("序号 / 事件"),
        tr("说明"),
        tr("来源")
    });
    staleLabel->setText(
        stale ? tr("结果已过期，跳转将使用当前源码")
              : tr("当前结果"));
    presentWarnings();
    presentRuntimeTraceIssues();
    updateStatusText();
    updateRuntimeTraceStatusText();
}

void StoryGraphWindow::presentCurrentGraph()
{
    const StoryGraphResult* graph =
        selectedGraph();
    const StoryGraphLayoutResult* layout =
        selectedLayout();
    if (!graph || !layout ||
        !layout->isUsable())
    {
        graphView->clearGraph();
        updateStatusText();
        return;
    }
    graphView->setGraph(
        *graph,
        *layout,
        currentPresentedGeneration);
    applyRuntimeTraceOverlay();
    updateStatusText();
}

void StoryGraphWindow::presentWarnings()
{
    warningTree->clear();
    if (!currentBundle)
    {
        if (!lastDiagnosticCode.isEmpty() ||
            !lastDiagnosticMessage.isEmpty())
        {
            auto* item = new QTreeWidgetItem(
                warningTree);
            item->setText(0, tr("错误"));
            item->setText(1, lastDiagnosticCode);
            item->setText(2, lastDiagnosticMessage);
        }
        return;
    }

    int presentedRows = 0;
    const QList<StoryGraphWarning>&
        projectWarnings =
            currentBundle->projectResult.warnings;
    for (int index = 0;
         index < projectWarnings.size() &&
         presentedRows < MaximumPresentedWarningRows;
         ++index)
    {
        const StoryGraphWarning& warning =
            projectWarnings.at(index);
        auto* item = new QTreeWidgetItem(
            warningTree);
        item->setText(
            0,
            warningSeverityText(warning.severity));
        item->setText(1, warning.diagnosticCode);
        item->setText(2, warning.message);
        item->setText(3, sourceLocationText(warning));
        item->setData(
            0,
            Qt::UserRole,
            static_cast<int>(warning.severity));
        ++presentedRows;
    }
    const StoryGraphLayoutResult* layout =
        selectedLayout();
    if (layout)
    {
        for (int index = 0;
             index < layout->warnings.size() &&
             presentedRows <
                 MaximumPresentedWarningRows;
             ++index)
        {
            const StoryGraphLayoutWarning& warning =
                layout->warnings.at(index);
            auto* item = new QTreeWidgetItem(
                warningTree);
            item->setText(0, tr("警告"));
            item->setText(
                1, warning.diagnosticCode);
            item->setText(2, warning.message);
            item->setText(
                3, warning.relatedNodeId);
            ++presentedRows;
        }
    }
    const int totalWarningRows =
        projectWarnings.size() +
        (layout ? layout->warnings.size() : 0);
    if (presentedRows < totalWarningRows)
    {
        auto* item = new QTreeWidgetItem(
            warningTree);
        item->setText(0, tr("信息"));
        item->setText(
            1,
            QStringLiteral(
                "story_graph.ui.warning_rows_limited"));
        item->setText(
            2,
            tr("仅显示前 %1 条警告；另有 %2 条未在列表中展开。")
                .arg(MaximumPresentedWarningRows)
                .arg(totalWarningRows -
                     presentedRows));
    }
    warningTree->resizeColumnToContents(0);
    warningTree->resizeColumnToContents(1);
}

void StoryGraphWindow::refreshRuntimeTrace()
{
    if (currentRuntimeTraceSessionId.isEmpty())
    {
        runtimeTraceRefreshTimer->stop();
        return;
    }

    const StoryGraphRuntimeTraceStreamState
        stateBeforeRefresh =
            runtimeTraceTailer.state();
    const qsizetype issueCountBeforeRefresh =
        runtimeTraceTailer.issues().size();
    const qsizetype eventCountBeforeRefresh =
        runtimeTraceTailer.eventCount();
    const quint64 matcherRevisionBeforeRefresh =
        runtimeTraceMatcher.result().revision;
    const bool matcherFailedBeforeRefresh =
        runtimeTraceMatcherFailed;
    const qsizetype graphIssueCountBeforeRefresh =
        selectedGraphKind() ==
                StoryGraphKind::ControlFlow
            ? runtimeTraceMatcher.result().
                  controlFlowIssues.issueCount
            : runtimeTraceMatcher.result().
                  storySemanticsIssues.issueCount;

    if (runtimeTraceProducerTerminal &&
        !runtimeTraceFinalizationIssued)
    {
        runtimeTraceTailer.finalize(
            runtimeTraceForcedTermination);
        runtimeTraceFinalizationIssued = true;
    }
    else
    {
        runtimeTraceTailer.refresh();
    }

    const qsizetype eventCount =
        runtimeTraceTailer.eventCount();
    if (eventCount <
            appliedRuntimeTraceEventCount)
    {
        appliedRuntimeTraceEventCount = 0;
        runtimeTraceMatcher.resetTrace(
            currentRuntimeTraceSessionId);
        runtimeTraceMatcherFailed = false;
        graphView->clearTraceOverlay();
    }

    if (appliedRuntimeTraceEventCount <
            eventCount)
    {
        appendRuntimeTraceEvents(
            appliedRuntimeTraceEventCount,
            eventCount);
    }

    const bool presentationChanged =
        stateBeforeRefresh !=
            runtimeTraceTailer.state() ||
        issueCountBeforeRefresh !=
            runtimeTraceTailer.issues().size() ||
        eventCountBeforeRefresh != eventCount ||
        matcherRevisionBeforeRefresh !=
            runtimeTraceMatcher.result().revision ||
        matcherFailedBeforeRefresh !=
            runtimeTraceMatcherFailed;
    if (presentationChanged)
    {
        applyRuntimeTraceOverlay();
        const qsizetype graphIssueCountAfterRefresh =
            selectedGraphKind() ==
                    StoryGraphKind::ControlFlow
                ? runtimeTraceMatcher.result().
                      controlFlowIssues.issueCount
                : runtimeTraceMatcher.result().
                      storySemanticsIssues.issueCount;
        if (issueCountBeforeRefresh !=
                runtimeTraceTailer.issues().size() ||
            matcherFailedBeforeRefresh !=
                runtimeTraceMatcherFailed ||
            graphIssueCountBeforeRefresh !=
                graphIssueCountAfterRefresh)
        {
            presentRuntimeTraceIssues();
        }
        updateRuntimeTraceStatusText();
    }

    if (runtimeTraceTailer.state() ==
            StoryGraphRuntimeTraceStreamState::
                Invalid ||
        (runtimeTraceProducerTerminal &&
         !runtimeTraceTailer.hasUnreadBytes()))
    {
        runtimeTraceRefreshTimer->stop();
    }
}

void StoryGraphWindow::rematchRuntimeTrace()
{
    runtimeTraceMatcher.clearProjectResult();
    if (stale || !currentBundle)
    {
        graphView->clearTraceOverlay();
        presentRuntimeTraceIssues();
        updateRuntimeTraceStatusText();
        return;
    }

    runtimeTraceMatcher.setProjectResult(
        currentBundle->projectResult);
    const qsizetype eventCount =
        runtimeTraceTailer.eventCount();
    if (runtimeTraceTailer.
            discardedThroughSequence() == 0)
    {
        runtimeTraceMatcher.resetTrace(
            currentRuntimeTraceSessionId);
        runtimeTraceMatcherFailed = false;
        appliedRuntimeTraceEventCount = 0;
        if (!currentRuntimeTraceSessionId.
                isEmpty() &&
            eventCount > 0)
        {
            appendRuntimeTraceEvents(
                0,
                eventCount);
        }
    }
    else if (!currentRuntimeTraceSessionId.
                 isEmpty())
    {
        const quint64 discardedSequence =
            runtimeTraceTailer.
                discardedThroughSequence();
        if (!runtimeTraceMatcher.
                 hasDiscardExecutionCheckpoint() ||
            runtimeTraceMatcher.
                discardExecutionCheckpointSequence() !=
                    discardedSequence ||
            !runtimeTraceMatcher.
                 restoreDiscardExecutionCheckpoint())
        {
            failRuntimeTraceMatching();
        }
        else
        {
            runtimeTraceMatcherFailed = false;
            appliedRuntimeTraceEventCount = 0;
            if (eventCount > 0)
            {
                appendRuntimeTraceEvents(
                    0,
                    eventCount);
            }
        }
    }
    applyRuntimeTraceOverlay();
    presentRuntimeTraceIssues();
    updateRuntimeTraceStatusText();
}

bool StoryGraphWindow::appendRuntimeTraceEvents(
    qsizetype firstIndex,
    qsizetype eventCount)
{
    if (runtimeTraceMatcherFailed)
        return false;

    const QVector<StoryGraphRuntimeTraceEvent>&
        events = runtimeTraceTailer.events();
    if (firstIndex < 0 ||
        firstIndex > eventCount ||
        eventCount != events.size())
    {
        failRuntimeTraceMatching();
        return false;
    }
    if (firstIndex == eventCount)
        return true;
    if (!runtimeTraceMatcher.appendEvents(
            events,
            firstIndex))
    {
        failRuntimeTraceMatching();
        return false;
    }
    appliedRuntimeTraceEventCount = eventCount;
    return true;
}

void StoryGraphWindow::failRuntimeTraceMatching()
{
    runtimeTraceMatcherFailed = true;
    runtimeTraceRefreshTimer->stop();
    graphView->clearTraceOverlay();
}

void StoryGraphWindow::applyRuntimeTraceOverlay()
{
    if (runtimeTraceMatcherFailed ||
        stale ||
        !currentBundle ||
        !runtimeTraceMatcher.hasProjectResult() ||
        runtimeTraceMatcher.analysisGeneration() !=
            currentPresentedGeneration)
    {
        graphView->clearTraceOverlay();
        return;
    }

    const StoryGraphTraceGraphOverlay& overlay =
        selectedGraphKind() ==
                StoryGraphKind::ControlFlow
            ? runtimeTraceMatcher.result().
                  controlFlow
            : runtimeTraceMatcher.result().
                  storySemantics;
    graphView->setTraceOverlay(
        overlay,
        currentPresentedGeneration);
}

void StoryGraphWindow::presentRuntimeTraceIssues()
{
    ++currentRuntimeTraceIssuePresentationRevision;
    ++currentRuntimeTraceIssuePresentationRebuildCount;
    runtimeTraceIssueTree->clear();
    int presentedRows = 0;

    if (runtimeTraceMatcherFailed)
    {
        ++currentRuntimeTraceIssuePresentationWorkItemCount;
        auto* item = new QTreeWidgetItem(
            runtimeTraceIssueTree);
        item->setText(0, tr("匹配问题"));
        item->setText(
            1,
            QStringLiteral(
                "runtime_trace.matcher.state_invalid"));
        item->setText(
            2,
            tr("运行轨迹匹配状态与已读取序列不一致；已停止继续匹配，避免显示错误执行路径。"));
        ++presentedRows;
    }

    const QVector<StoryGraphRuntimeTraceIssue>&
        streamIssues =
            runtimeTraceTailer.issues();
    for (int index = 0;
         index < streamIssues.size() &&
         presentedRows <
             MaximumPresentedRuntimeTraceRows;
         ++index)
    {
        const StoryGraphRuntimeTraceIssue& issue =
            streamIssues.at(index);
        ++currentRuntimeTraceIssuePresentationWorkItemCount;
        auto* item = new QTreeWidgetItem(
            runtimeTraceIssueTree);
        item->setText(0, tr("读取问题"));
        item->setText(
            1,
            storyGraphRuntimeTraceIssueCodeToString(
                issue.code));
        item->setText(
            2,
            tr("轨迹读取器报告：%1")
                .arg(issue.message));
        if (issue.lineNumber > 0)
        {
            item->setText(
                3,
                tr("JSONL 第 %1 行，字节 %2")
                    .arg(issue.lineNumber)
                    .arg(issue.byteOffset));
        }
        ++presentedRows;
    }

    const StoryGraphTraceGraphIssueSummary&
        graphIssues =
            selectedGraphKind() ==
                    StoryGraphKind::ControlFlow
                ? runtimeTraceMatcher.result().
                      controlFlowIssues
                : runtimeTraceMatcher.result().
                      storySemanticsIssues;
    for (int index = 0;
         index < graphIssues.
                     presentedIssues.size() &&
         presentedRows <
             MaximumPresentedRuntimeTraceRows;
         ++index)
    {
        const StoryGraphTraceIssuePresentation&
            presentation =
                graphIssues.presentedIssues.at(
                    index);
        const StoryGraphRuntimeTraceEvent& event =
            presentation.event;
        ++currentRuntimeTraceIssuePresentationWorkItemCount;
        const bool droppedSummary =
            event.type ==
            StoryGraphRuntimeTraceEventType::
                TraceDropped;

        auto* item = new QTreeWidgetItem(
            runtimeTraceIssueTree);
        item->setText(
            0,
            droppedSummary
                ? tr("丢弃汇总")
                : runtimeTraceMatchStatusText(
                      presentation.status));
        item->setText(
            1,
            QStringLiteral("%1 / %2")
                .arg(event.sequence)
                .arg(
                    storyGraphRuntimeTraceEventTypeToString(
                        event.type)));
        item->setText(
            2,
            runtimeTraceEventDetail(event));
        if (!event.virtualPath.isEmpty())
        {
            item->setText(
                3,
                event.line > 0
                    ? QStringLiteral("%1:%2")
                          .arg(event.virtualPath)
                          .arg(event.line)
                    : event.virtualPath);
        }
        ++presentedRows;
    }

    const qsizetype totalRows =
        (runtimeTraceMatcherFailed ? 1 : 0) +
        streamIssues.size() +
        graphIssues.issueCount;
    if (presentedRows < totalRows &&
        presentedRows <
            MaximumPresentedRuntimeTraceRows)
    {
        auto* item = new QTreeWidgetItem(
            runtimeTraceIssueTree);
        item->setText(0, tr("信息"));
        item->setText(
            1,
            QStringLiteral(
                "runtime_trace.ui.rows_limited"));
        item->setText(
            2,
            tr("仅显示前 %1 条轨迹问题；另有 %2 条未展开。")
                .arg(
                    MaximumPresentedRuntimeTraceRows)
                .arg(totalRows -
                     presentedRows));
    }
    runtimeTraceIssueTree->
        resizeColumnToContents(0);
    runtimeTraceIssueTree->
        resizeColumnToContents(1);
}

void StoryGraphWindow::updateStatusText()
{
    if (coordinator->isBusy())
    {
        statusLabel->setText(
            tr("正在后台分析剧情图…"));
        return;
    }
    if (!lastDiagnosticCode.isEmpty() ||
        !lastDiagnosticMessage.isEmpty())
    {
        const QString diagnosticSummary =
            !lastDiagnosticCode.isEmpty()
            ? lastDiagnosticCode
            : lastDiagnosticMessage;
        statusLabel->setText(
            tr("分析不可用：%1")
                .arg(diagnosticSummary));
        return;
    }
    if (!currentBundle)
    {
        statusLabel->setText(
            tr("尚未生成剧情图"));
        return;
    }

    const StoryGraphResult* graph =
        selectedGraph();
    const int totalNodes =
        graph ? graph->nodes.size() : 0;
    const StoryGraphLayoutResult* layout =
        selectedLayout();
    const int warningCount =
        currentBundle->
            projectResult.warnings.size() +
        (layout ? layout->warnings.size() : 0);
    statusLabel->setText(
        tr("%1；显示 %2 / %3 个节点；%4 条警告")
            .arg(
                projectStatusText(
                    currentBundle->
                        projectResult.status))
            .arg(graphView->visibleNodeCount())
            .arg(totalNodes)
            .arg(warningCount));
}

void StoryGraphWindow::
updateRuntimeTraceStatusText()
{
    const bool hasSession =
        !currentRuntimeTraceSessionId.isEmpty();
    clearRuntimeTraceButton->setEnabled(
        hasSession &&
        (runtimeTraceTailer.eventCount() > 0 ||
         !runtimeTraceMatcher.result().
              eventMatches.isEmpty()));
    if (!hasSession)
    {
        runtimeTraceStatusLabel->setText(
            tr("未选择运行轨迹会话"));
        return;
    }
    if (runtimeTraceMatcherFailed)
    {
        runtimeTraceStatusLabel->setText(
            tr("运行轨迹匹配已停止：内部序列状态不一致"));
        return;
    }

    const StoryGraphTraceMatchResult& result =
        runtimeTraceMatcher.result();
    runtimeTraceStatusLabel->setText(
        tr("%1；事件 %2；匹配 %3，未匹配 %4，歧义 %5，过期 %6；读取问题 %7；丢弃行 %8")
            .arg(
                runtimeTraceStreamStateText(
                    runtimeTraceTailer.state()))
            .arg(runtimeTraceTailer.eventCount())
            .arg(result.matchedEventCount())
            .arg(result.unmatchedEventCount())
            .arg(result.ambiguousEventCount())
            .arg(result.staleEventCount())
            .arg(runtimeTraceTailer.issues().size())
            .arg(result.droppedSourceLineCount));
}

void StoryGraphWindow::setStale(bool nextStale)
{
    stale = nextStale;
    if (stale)
    {
        runtimeTraceMatcher.clearProjectResult();
        graphView->clearTraceOverlay();
        presentRuntimeTraceIssues();
        updateRuntimeTraceStatusText();
    }
    staleLabel->setText(
        stale ? tr("结果已过期，跳转将使用当前源码")
              : tr("当前结果"));
}

QString StoryGraphWindow::warningSeverityText(
    StoryGraphWarningSeverity severity) const
{
    switch (severity)
    {
    case StoryGraphWarningSeverity::Information:
        return tr("信息");
    case StoryGraphWarningSeverity::Warning:
        return tr("警告");
    case StoryGraphWarningSeverity::Error:
        return tr("错误");
    }
    return tr("警告");
}

QString StoryGraphWindow::
runtimeTraceStreamStateText(
    StoryGraphRuntimeTraceStreamState state) const
{
    switch (state)
    {
    case StoryGraphRuntimeTraceStreamState::Unbound:
        return tr("未绑定");
    case StoryGraphRuntimeTraceStreamState::WaitingForFile:
        return tr("等待轨迹文件");
    case StoryGraphRuntimeTraceStreamState::Live:
        return tr("实时");
    case StoryGraphRuntimeTraceStreamState::Complete:
        return tr("完整");
    case StoryGraphRuntimeTraceStreamState::Incomplete:
        return tr("不完整");
    case StoryGraphRuntimeTraceStreamState::Invalid:
        return tr("无效");
    }
    return tr("状态未知");
}

QString StoryGraphWindow::
runtimeTraceMatchStatusText(
    StoryGraphTraceMatchStatus status) const
{
    switch (status)
    {
    case StoryGraphTraceMatchStatus::NotApplicable:
        return tr("不适用");
    case StoryGraphTraceMatchStatus::Matched:
        return tr("已匹配");
    case StoryGraphTraceMatchStatus::Unmatched:
        return tr("未匹配");
    case StoryGraphTraceMatchStatus::Ambiguous:
        return tr("歧义");
    case StoryGraphTraceMatchStatus::Stale:
        return tr("过期");
    }
    return tr("状态未知");
}

StoryGraphKind StoryGraphWindow::selectedGraphKind() const
{
    if (!graphModeCombo ||
        graphModeCombo->currentIndex() < 0)
    {
        return StoryGraphKind::ControlFlow;
    }
    return static_cast<StoryGraphKind>(
        graphModeCombo->currentData().toInt());
}

const StoryGraphResult*
StoryGraphWindow::selectedGraph() const
{
    if (!currentBundle)
        return nullptr;
    return selectedGraphKind() ==
            StoryGraphKind::ControlFlow
        ? &currentBundle->
              projectResult.controlFlowGraph
        : &currentBundle->
              projectResult.semanticGraph;
}

const StoryGraphLayoutResult*
StoryGraphWindow::selectedLayout() const
{
    if (!currentBundle)
        return nullptr;
    return selectedGraphKind() ==
            StoryGraphKind::ControlFlow
        ? &currentBundle->controlFlowLayout
        : &currentBundle->semanticLayout;
}
