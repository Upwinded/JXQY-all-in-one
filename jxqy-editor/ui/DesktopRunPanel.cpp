#include "DesktopRunPanel.h"

#include "DesktopRunDiagnosticsModel.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
QString executableDisplayText(const QString& executablePath)
{
    const QString fileName =
        QFileInfo(executablePath).fileName();
    return fileName.isEmpty()
        ? executablePath
        : fileName;
}

void configureCompactValueLabel(QLabel* label)
{
    label->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    label->setWordWrap(false);
    label->setSizePolicy(
        QSizePolicy::Ignored,
        QSizePolicy::Preferred);
}
}

DesktopRunPanel::DesktopRunPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("desktopRunPanel"));

    sceneComboBox = new QComboBox(this);
    sceneComboBox->setObjectName(
        QStringLiteral("desktopRunSceneComboBox"));
    targetCaptionLabel = new QLabel(this);
    playtestTargetValueLabel = new QLabel(this);
    playtestTargetValueLabel->setObjectName(
        QStringLiteral("desktopRunTargetValueLabel"));
    configureCompactValueLabel(
        playtestTargetValueLabel);
    stateCaptionLabel = new QLabel(this);
    resultCaptionLabel = new QLabel(this);

    activePackValueLabel = new QLabel(this);
    activePackValueLabel->setObjectName(
        QStringLiteral("desktopRunActivePackValueLabel"));
    configureCompactValueLabel(activePackValueLabel);

    sessionTargetValueLabel = new QLabel(this);
    sessionTargetValueLabel->setObjectName(
        QStringLiteral("desktopRunSessionTargetValueLabel"));
    configureCompactValueLabel(sessionTargetValueLabel);

    sessionPackValueLabel = new QLabel(this);
    sessionPackValueLabel->setObjectName(
        QStringLiteral("desktopRunSessionPackValueLabel"));
    configureCompactValueLabel(sessionPackValueLabel);

    executableValueLabel = new QLabel(this);
    executableValueLabel->setObjectName(
        QStringLiteral("desktopRunExecutableValueLabel"));
    configureCompactValueLabel(executableValueLabel);

    chooseExecutableButton = new QPushButton(this);
    chooseExecutableButton->setObjectName(
        QStringLiteral("desktopRunChooseExecutableButton"));

    auto* executableLayout = new QHBoxLayout;
    executableLayout->setContentsMargins(0, 0, 0, 0);
    executableLayout->addWidget(executableValueLabel, 1);
    executableLayout->addWidget(chooseExecutableButton);

    sessionValueLabel = new QLabel(this);
    sessionValueLabel->setObjectName(
        QStringLiteral("desktopRunSessionValueLabel"));
    configureCompactValueLabel(sessionValueLabel);

    overlayValueLabel = new QLabel(this);
    overlayValueLabel->setObjectName(
        QStringLiteral("desktopRunOverlayValueLabel"));
    configureCompactValueLabel(overlayValueLabel);

    stateValueLabel = new QLabel(this);
    stateValueLabel->setObjectName(
        QStringLiteral("desktopRunStateValueLabel"));
    configureCompactValueLabel(stateValueLabel);
    progressValueLabel = new QLabel(this);
    progressValueLabel->setObjectName(
        QStringLiteral("desktopRunProgressValueLabel"));
    configureCompactValueLabel(progressValueLabel);
    exitValueLabel = new QLabel(this);
    exitValueLabel->setObjectName(
        QStringLiteral("desktopRunExitValueLabel"));
    configureCompactValueLabel(exitValueLabel);

    runButton = new QPushButton(this);
    runButton->setObjectName(
        QStringLiteral("desktopRunStartButton"));
    stopButton = new QPushButton(this);
    stopButton->setObjectName(
        QStringLiteral("desktopRunStopButton"));
    problemButton = new QToolButton(this);
    problemButton->setObjectName(
        QStringLiteral("desktopRunProblemButton"));
    problemButton->setToolButtonStyle(
        Qt::ToolButtonTextOnly);
    problemButton->setVisible(false);
    detailsToggleButton = new QToolButton(this);
    detailsToggleButton->setObjectName(
        QStringLiteral("desktopRunDetailsToggleButton"));
    detailsToggleButton->setCheckable(true);
    detailsToggleButton->setArrowType(Qt::RightArrow);
    detailsToggleButton->setToolButtonStyle(
        Qt::ToolButtonTextBesideIcon);

    outputEdit = new QPlainTextEdit(this);
    outputEdit->setObjectName(
        QStringLiteral("desktopRunOutputEdit"));
    outputEdit->setReadOnly(true);
    outputEdit->setMaximumBlockCount(10000);

    diagnostics = new DesktopRunDiagnosticsModel(this);
    diagnostics->setObjectName(
        QStringLiteral("desktopRunDiagnosticsModel"));
    diagnosticsView = new QTableView(this);
    diagnosticsView->setObjectName(
        QStringLiteral("desktopRunDiagnosticsView"));
    diagnosticsView->setModel(diagnostics);
    diagnosticsView->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    diagnosticsView->setSelectionMode(
        QAbstractItemView::SingleSelection);
    diagnosticsView->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    diagnosticsView->setAlternatingRowColors(true);
    diagnosticsView->verticalHeader()->setVisible(false);
    diagnosticsView->horizontalHeader()->
        setStretchLastSection(false);
    diagnosticsView->horizontalHeader()->
        setSectionResizeMode(
            DesktopRunDiagnosticsModel::SeverityColumn,
            QHeaderView::ResizeToContents);
    diagnosticsView->horizontalHeader()->
        setSectionResizeMode(
            DesktopRunDiagnosticsModel::CodeColumn,
            QHeaderView::ResizeToContents);
    diagnosticsView->horizontalHeader()->
        setSectionResizeMode(
            DesktopRunDiagnosticsModel::MessageColumn,
            QHeaderView::Stretch);
    diagnosticsView->horizontalHeader()->
        setSectionResizeMode(
            DesktopRunDiagnosticsModel::LocationColumn,
            QHeaderView::ResizeToContents);
    diagnosticsView->horizontalHeader()->
        setSectionResizeMode(
            DesktopRunDiagnosticsModel::TargetColumn,
            QHeaderView::ResizeToContents);

    diagnosticsIssueLabel = new QLabel(this);
    diagnosticsIssueLabel->setObjectName(
        QStringLiteral("desktopRunDiagnosticsIssueLabel"));
    diagnosticsIssueLabel->setWordWrap(true);

    auto* diagnosticsWidget = new QWidget(this);
    auto* diagnosticsLayout =
        new QVBoxLayout(diagnosticsWidget);
    diagnosticsLayout->setContentsMargins(0, 0, 0, 0);
    diagnosticsLayout->addWidget(diagnosticsView, 1);
    diagnosticsLayout->addWidget(diagnosticsIssueLabel);

    outputTabs = new QTabWidget(this);
    outputTabs->setObjectName(
        QStringLiteral("desktopRunOutputTabs"));
    outputTabs->addTab(outputEdit, QString());
    outputTabs->addTab(diagnosticsWidget, QString());

    detailsFormLayout = new QFormLayout;
    detailsFormLayout->setFieldGrowthPolicy(
        QFormLayout::ExpandingFieldsGrow);
    detailsFormLayout->addRow(
        QString(), sceneComboBox);
    detailsFormLayout->addRow(
        QString(), executableLayout);
    detailsFormLayout->addRow(
        QString(), activePackValueLabel);
    detailsFormLayout->addRow(
        QString(), sessionTargetValueLabel);
    detailsFormLayout->addRow(
        QString(), sessionPackValueLabel);
    detailsFormLayout->addRow(
        QString(), sessionValueLabel);
    detailsFormLayout->addRow(
        QString(), overlayValueLabel);
    detailsFormLayout->addRow(
        QString(), progressValueLabel);

    detailsWidget = new QWidget(this);
    detailsWidget->setObjectName(
        QStringLiteral("desktopRunDetailsWidget"));
    auto* detailsLayout =
        new QVBoxLayout(detailsWidget);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->addLayout(detailsFormLayout);
    detailsLayout->addWidget(outputTabs, 1);
    detailsWidget->setVisible(false);

    auto* summaryLayout = new QGridLayout;
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setHorizontalSpacing(8);
    summaryLayout->setVerticalSpacing(4);
    summaryLayout->addWidget(targetCaptionLabel, 0, 0);
    summaryLayout->addWidget(
        playtestTargetValueLabel, 0, 1, 1, 3);
    summaryLayout->addWidget(problemButton, 0, 4, 1, 2);
    summaryLayout->addWidget(stateCaptionLabel, 1, 0);
    summaryLayout->addWidget(stateValueLabel, 1, 1);
    summaryLayout->addWidget(resultCaptionLabel, 1, 2);
    summaryLayout->addWidget(exitValueLabel, 1, 3);
    summaryLayout->addWidget(runButton, 1, 4);
    summaryLayout->addWidget(stopButton, 1, 5);
    summaryLayout->addWidget(detailsToggleButton, 1, 6);
    summaryLayout->setColumnStretch(1, 2);
    summaryLayout->setColumnStretch(3, 1);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);
    mainLayout->addLayout(summaryLayout);
    mainLayout->addWidget(detailsWidget, 1);

    diagnosticsRefreshTimer = new QTimer(this);
    diagnosticsRefreshTimer->setObjectName(
        QStringLiteral(
            "desktopRunDiagnosticsRefreshTimer"));
    diagnosticsRefreshTimer->setInterval(200);

    connect(
        sceneComboBox,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this]()
        {
            updateButtonStates();
        });
    connect(
        runButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            emit playtestRequested();
        });
    connect(
        stopButton,
        &QPushButton::clicked,
        this,
        &DesktopRunPanel::stopRequested);
    connect(
        chooseExecutableButton,
        &QPushButton::clicked,
        this,
        &DesktopRunPanel::
            desktopExecutableSelectionRequested);
    connect(
        detailsToggleButton,
        &QToolButton::toggled,
        this,
        [this](bool expanded)
        {
            detailsWidget->setVisible(expanded);
            detailsToggleButton->setArrowType(
                expanded
                ? Qt::DownArrow
                : Qt::RightArrow);
            emit detailsExpandedChanged(expanded);
        });
    connect(
        problemButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            int firstProblemRow = -1;
            for (int row = 0;
                 row < diagnostics->rowCount();
                 ++row)
            {
                const DesktopRunDiagnosticsModel::Record*
                    record = diagnostics->recordAt(row);
                if (!record ||
                    record->severity ==
                        DesktopRunDiagnosticsModel::Severity::Info)
                {
                    continue;
                }
                if (firstProblemRow < 0)
                    firstProblemRow = row;
                if (record->source.isValid())
                {
                    emit sourceLocationRequested(
                        currentSessionId,
                        record->source.file,
                        record->source.line,
                        record->source.column);
                    return;
                }
            }
            if (firstProblemRow < 0)
                return;
            detailsToggleButton->setChecked(true);
            outputTabs->setCurrentIndex(1);
            diagnosticsView->selectRow(firstProblemRow);
        });
    connect(
        diagnosticsRefreshTimer,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (QFileInfo::exists(
                    diagnostics->diagnosticsPath()))
            {
                diagnostics->refresh();
            }
        });
    connect(
        diagnostics,
        &DesktopRunDiagnosticsModel::issuesChanged,
        this,
        &DesktopRunPanel::updateDiagnosticsIssues);
    connect(
        diagnostics,
        &QAbstractItemModel::rowsInserted,
        this,
        [this]()
        {
            updateProblemShortcut();
        });
    connect(
        diagnostics,
        &QAbstractItemModel::modelReset,
        this,
        &DesktopRunPanel::updateProblemShortcut);
    connect(
        diagnosticsView,
        &QTableView::doubleClicked,
        this,
        [this](const QModelIndex& index)
        {
            const DesktopRunDiagnosticsModel::
                SourceLocation source =
                    diagnostics->sourceLocationAt(
                        index.row());
            if (source.isValid())
            {
                emit sourceLocationRequested(
                    currentSessionId,
                    source.file,
                    source.line,
                    source.column);
            }
        });

    retranslateUi();
    updateButtonStates();
    updateDiagnosticsIssues();
    updateProblemShortcut();
}

void DesktopRunPanel::setProjectRuntimeConfiguration(
    const ProjectRuntimeConfiguration& configuration,
    bool projectAvailable)
{
    const QString selectedId = selectedSceneId();
    currentConfiguration = configuration;
    currentProjectAvailable = projectAvailable;
    sceneComboBox->clear();
    QHash<QString, int> sceneNameCounts;
    QHash<QString, int> sceneNameOrdinals;
    for (const ProjectScene& scene : configuration.scenes)
        ++sceneNameCounts[scene.name];
    int selectedIndex = -1;
    int defaultIndex = -1;
    for (const ProjectScene& scene : configuration.scenes)
    {
        const int index = sceneComboBox->count();
        QString displayText = scene.name;
        if (displayText.isEmpty())
            displayText = tr("未命名场景");
        if (sceneNameCounts.value(scene.name) > 1)
        {
            displayText +=
                QStringLiteral(" (%1)")
                    .arg(++sceneNameOrdinals[scene.name]);
        }
        sceneComboBox->addItem(
            displayText,
            scene.id);
        sceneComboBox->setItemData(
            index,
            tr("场景 ID：%1").arg(scene.id),
            Qt::ToolTipRole);
        if (scene.id == selectedId)
            selectedIndex = index;
        if (scene.id == configuration.defaultSceneId)
            defaultIndex = index;
    }
    if (selectedIndex < 0)
        selectedIndex = defaultIndex;
    if (selectedIndex < 0 &&
        sceneComboBox->count() > 0)
    {
        selectedIndex = 0;
    }
    sceneComboBox->setCurrentIndex(selectedIndex);
    updateButtonStates();
}

void DesktopRunPanel::setActiveResourcePackId(
    const QString& resourcePackId)
{
    currentResourcePackId = resourcePackId;
    activePackValueLabel->setText(
        resourcePackId.isEmpty()
        ? tr("不可用")
        : resourcePackId);
    activePackValueLabel->setToolTip(resourcePackId);
}

void DesktopRunPanel::setDesktopExecutable(
    const QString& executablePath,
    bool valid)
{
    currentExecutablePath = executablePath;
    currentExecutableValid = valid;
    executableValueLabel->setText(
        executablePath.isEmpty()
        ? tr("未配置")
        : executableDisplayText(executablePath));
    executableValueLabel->setToolTip(
        valid || executablePath.isEmpty()
        ? executablePath
        : executablePath + QStringLiteral("\n") +
              tr("配置的桌面游戏可执行文件无效。"));
    updateButtonStates();
}

void DesktopRunPanel::setPlaytestTargetText(
    const QString& text)
{
    currentPlaytestTargetText = text;
    playtestTargetValueLabel->setText(text);
    playtestTargetValueLabel->setToolTip(text);
}

void DesktopRunPanel::setAuthoringOperationInProgress(
    bool inProgress)
{
    if (currentAuthoringOperationInProgress ==
        inProgress)
    {
        return;
    }
    currentAuthoringOperationInProgress =
        inProgress;
    updateButtonStates();
}

void DesktopRunPanel::setSessionCleanupInProgress(
    bool inProgress)
{
    if (currentSessionCleanupInProgress == inProgress)
        return;
    currentSessionCleanupInProgress = inProgress;
    if (inProgress)
    {
        progressValueLabel->setText(
            tr("正在清理上次试玩的临时文件……"));
        setCursor(Qt::WaitCursor);
    }
    else
    {
        unsetCursor();
        if (currentState ==
            DesktopRunSessionCoordinatorState::Preparing)
        {
            progressValueLabel->setText(
                tr("正在准备私有运行会话……"));
        }
        else
        {
            progressValueLabel->clear();
        }
    }
    updateButtonStates();
    progressValueLabel->repaint();
    runButton->repaint();
}

QString DesktopRunPanel::selectedSceneId() const
{
    return sceneComboBox->currentData().toString();
}

void DesktopRunPanel::setCoordinatorState(
    DesktopRunSessionCoordinatorState state)
{
    currentState = state;
    stateValueLabel->setText(stateText(state));
    if (state == DesktopRunSessionCoordinatorState::Preparing)
    {
        currentSessionId.clear();
        currentSessionPath.clear();
        currentSessionTarget.clear();
        currentSessionResourcePackId.clear();
        currentOverlayPath.clear();
        sessionTargetValueLabel->setText(
            tr("正在创建私有会话……"));
        sessionTargetValueLabel->setToolTip(QString());
        sessionPackValueLabel->setText(
            tr("正在创建私有会话……"));
        sessionPackValueLabel->setToolTip(QString());
        sessionValueLabel->setText(
            tr("正在创建私有会话……"));
        sessionValueLabel->setToolTip(QString());
        overlayValueLabel->setText(
            tr("正在创建私有会话……"));
        overlayValueLabel->setToolTip(QString());
        diagnosticsRefreshTimer->stop();
        diagnostics->setSource(QString(), QString());
        outputEdit->clear();
        pendingStandardOutput.clear();
        pendingStandardError.clear();
        exitValueLabel->clear();
        exitValueLabel->setToolTip(QString());
        progressValueLabel->setText(
            tr("正在准备私有运行会话……"));
    }
    if (state == DesktopRunSessionCoordinatorState::Idle)
    {
        progressValueLabel->clear();
        exitValueLabel->clear();
    }
    updateButtonStates();
}

void DesktopRunPanel::setSession(
    const DesktopRunSessionPresentation& session,
    bool diagnosticsProducerActive)
{
    currentSessionId = session.sessionId;
    currentSessionPath = session.paths.sessionRoot;
    const QString targetIdentity =
        session.sceneName.trimmed().isEmpty()
        ? tr("未命名场景")
        : session.sceneName.trimmed();
    QString targetToolTip;
    switch (session.targetKind)
    {
    case EditorRun::TargetKind::Map:
        currentSessionTarget =
            tr("当前地图：%1")
                .arg(session.mapPath);
        break;
    case EditorRun::TargetKind::Script:
        currentSessionTarget =
            tr("当前脚本：%1")
                .arg(session.entryScriptPath);
        break;
    case EditorRun::TargetKind::Scene:
    default:
        currentSessionTarget =
            targetIdentity;
        targetToolTip =
            tr("场景 ID：%1")
                .arg(session.sceneId);
        break;
    }
    currentSessionResourcePackId =
        session.activeResourcePackId;
    currentOverlayPath = session.paths.overlayRoot;
    sessionTargetValueLabel->setText(
        currentSessionTarget);
    sessionTargetValueLabel->setToolTip(
        targetToolTip.isEmpty()
        ? currentSessionTarget
        : currentSessionTarget +
              QStringLiteral("\n") +
              targetToolTip);
    sessionPackValueLabel->setText(
        currentSessionResourcePackId);
    sessionPackValueLabel->setToolTip(
        currentSessionResourcePackId);
    sessionValueLabel->setText(session.paths.sessionRoot);
    sessionValueLabel->setToolTip(session.paths.sessionRoot);
    overlayValueLabel->setText(session.paths.overlayRoot);
    overlayValueLabel->setToolTip(session.paths.overlayRoot);
    diagnostics->setSource(
        session.paths.diagnosticsPath,
        session.sessionId);
    diagnosticsRefreshTimer->stop();
    if (!session.paths.diagnosticsPath.isEmpty())
    {
        if (diagnosticsProducerActive)
        {
            diagnosticsRefreshTimer->start();
        }
        else
        {
            diagnostics->finalizeStream();
        }
    }
    outputEdit->clear();
    pendingStandardOutput.clear();
    pendingStandardError.clear();
    exitValueLabel->clear();
    updateButtonStates();
}

void DesktopRunPanel::appendStandardOutput(
    const QString& text)
{
    appendOutput(QStringLiteral("stdout"), text);
}

void DesktopRunPanel::appendStandardError(
    const QString& text)
{
    appendOutput(QStringLiteral("stderr"), text);
}

void DesktopRunPanel::setTerminalResult(
    const DesktopRunSessionTerminalResult& result)
{
    diagnosticsRefreshTimer->stop();
    flushPendingOutput();
    if (!diagnostics->diagnosticsPath().isEmpty())
        diagnostics->finalizeStream();
    currentSessionId = result.sessionId;
    currentSessionPath = result.sessionPath;
    if (!result.sessionPath.isEmpty())
    {
        sessionValueLabel->setText(
            result.sessionPath);
        sessionValueLabel->setToolTip(
            result.sessionPath);
    }
    QString summary = outcomeText(result.outcome);
    QString technicalSummary = summary;
    if (result.processOutcome != DesktopRunOutcome::None)
    {
        technicalSummary += tr("；退出代码 %1")
            .arg(result.exitCode);
    }
    if (result.forcedKill)
        technicalSummary += tr("；已强制终止");
    if (!result.message.isEmpty())
    {
        technicalSummary +=
            QStringLiteral("；") + result.message;
    }
    exitValueLabel->setText(summary);
    exitValueLabel->setToolTip(technicalSummary);
    updateButtonStates();
}

bool DesktopRunPanel::detailsExpanded() const
{
    return detailsToggleButton &&
        detailsToggleButton->isChecked();
}

void DesktopRunPanel::clearSessionPresentation()
{
    diagnosticsRefreshTimer->stop();
    diagnostics->setSource(QString(), QString());
    currentSessionId.clear();
    currentSessionPath.clear();
    currentSessionTarget.clear();
    currentSessionResourcePackId.clear();
    currentOverlayPath.clear();
    pendingStandardOutput.clear();
    pendingStandardError.clear();
    outputEdit->clear();
    sessionTargetValueLabel->clear();
    sessionTargetValueLabel->setToolTip(QString());
    sessionPackValueLabel->clear();
    sessionPackValueLabel->setToolTip(QString());
    sessionValueLabel->clear();
    sessionValueLabel->setToolTip(QString());
    overlayValueLabel->clear();
    overlayValueLabel->setToolTip(QString());
    progressValueLabel->clear();
    exitValueLabel->clear();
    exitValueLabel->setToolTip(QString());
    retranslateUi();
    updateButtonStates();
}

DesktopRunDiagnosticsModel*
DesktopRunPanel::diagnosticsModel() const
{
    return diagnostics;
}

void DesktopRunPanel::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void DesktopRunPanel::updateButtonStates()
{
    const bool active =
        currentState ==
            DesktopRunSessionCoordinatorState::Preparing ||
        currentState ==
            DesktopRunSessionCoordinatorState::Starting ||
        currentState ==
            DesktopRunSessionCoordinatorState::Running ||
        currentState ==
            DesktopRunSessionCoordinatorState::Stopping;
    runButton->setEnabled(
        currentProjectAvailable &&
        !currentAuthoringOperationInProgress &&
        !currentSessionCleanupInProgress &&
        !active);
    stopButton->setEnabled(
        !currentSessionCleanupInProgress &&
        (currentState ==
             DesktopRunSessionCoordinatorState::Preparing ||
         currentState ==
             DesktopRunSessionCoordinatorState::Starting ||
         currentState ==
             DesktopRunSessionCoordinatorState::Running));
    sceneComboBox->setEnabled(
        currentProjectAvailable &&
        !currentAuthoringOperationInProgress &&
        !currentSessionCleanupInProgress &&
        !active);
    chooseExecutableButton->setEnabled(
        !currentAuthoringOperationInProgress &&
        !currentSessionCleanupInProgress &&
        !active);
}

void DesktopRunPanel::updateDiagnosticsIssues()
{
    const QVector<DesktopRunDiagnosticsModel::Issue>& issues =
        diagnostics->issues();
    if (issues.isEmpty())
    {
        diagnosticsIssueLabel->clear();
        return;
    }
    const DesktopRunDiagnosticsModel::Issue& issue =
        issues.constLast();
    diagnosticsIssueLabel->setText(
        tr("诊断流有 %1 个问题。最新问题：%2")
            .arg(issues.size())
            .arg(issue.message));
}

void DesktopRunPanel::updateProblemShortcut()
{
    int problemCount = 0;
    bool hasSourceLocation = false;
    for (int row = 0;
         row < diagnostics->rowCount();
         ++row)
    {
        const DesktopRunDiagnosticsModel::Record* record =
            diagnostics->recordAt(row);
        if (!record ||
            record->severity ==
                DesktopRunDiagnosticsModel::Severity::Info)
        {
            continue;
        }
        ++problemCount;
        hasSourceLocation =
            hasSourceLocation ||
            record->source.isValid();
    }

    problemButton->setVisible(problemCount > 0);
    if (problemCount <= 0)
    {
        problemButton->setText(QString());
        problemButton->setToolTip(QString());
        return;
    }

    problemButton->setText(
        hasSourceLocation
        ? tr("转到问题（%1）").arg(problemCount)
        : tr("查看问题（%1）").arg(problemCount));
    problemButton->setToolTip(
        hasSourceLocation
        ? tr("回到编辑器中的第一个问题位置")
        : tr("展开详细信息查看试玩问题"));
}

void DesktopRunPanel::retranslateUi()
{
    targetCaptionLabel->setText(tr("目标："));
    stateCaptionLabel->setText(tr("状态："));
    resultCaptionLabel->setText(tr("结果："));
    if (currentPlaytestTargetText.isEmpty())
    {
        playtestTargetValueLabel->setText(
            tr("请先打开项目"));
    }

    // QFormLayout rows containing layouts do not expose their labels through
    // labelForField(). Assign every generated label by stable row order.
    const QStringList detailLabels = {
        tr("场景选择："),
        tr("游戏程序："),
        tr("下次运行 Game.Id："),
        tr("当前运行目标："),
        tr("当前运行 Game.Id："),
        tr("会话目录："),
        tr("覆盖层目录："),
        tr("准备进度：")
    };
    const auto assignFormLabels =
        [](QFormLayout* layout,
           const QStringList& labels)
        {
            if (!layout)
                return;
            for (int row = 0;
                 row < labels.size() &&
                 row < layout->rowCount();
                 ++row)
            {
                QLayoutItem* labelItem =
                    layout->itemAt(
                        row,
                        QFormLayout::LabelRole);
                if (labelItem)
                {
                    if (QLabel* label =
                            qobject_cast<QLabel*>(
                                labelItem->widget()))
                    {
                        label->setText(labels.at(row));
                    }
                }
            }
        };
    assignFormLabels(detailsFormLayout, detailLabels);

    if (detailsToggleButton)
    {
        detailsToggleButton->setText(
            tr("日志与详细信息"));
    }

    chooseExecutableButton->setText(tr("选择……"));
    runButton->setText(tr("试玩"));
    stopButton->setText(tr("停止"));
    stateValueLabel->setText(stateText(currentState));
    if (currentSessionCleanupInProgress)
    {
        progressValueLabel->setText(
            tr("正在清理上次试玩的临时文件……"));
    }
    activePackValueLabel->setText(
        currentResourcePackId.isEmpty()
        ? tr("不可用")
        : currentResourcePackId);
    executableValueLabel->setText(
        currentExecutablePath.isEmpty()
        ? tr("未配置")
        : executableDisplayText(currentExecutablePath));
    if (currentSessionPath.isEmpty() &&
        currentState !=
            DesktopRunSessionCoordinatorState::Preparing)
    {
        sessionValueLabel->setText(tr("尚未创建"));
    }
    if (currentSessionTarget.isEmpty() &&
        currentState !=
            DesktopRunSessionCoordinatorState::Preparing)
    {
        sessionTargetValueLabel->setText(
            tr("尚未创建"));
    }
    if (currentSessionResourcePackId.isEmpty() &&
        currentState !=
            DesktopRunSessionCoordinatorState::Preparing)
    {
        sessionPackValueLabel->setText(
            tr("尚未创建"));
    }
    if (currentOverlayPath.isEmpty() &&
        currentState !=
            DesktopRunSessionCoordinatorState::Preparing)
    {
        overlayValueLabel->setText(
            tr("尚未创建"));
    }

    if (outputTabs)
    {
        outputTabs->setTabText(0, tr("输出"));
        outputTabs->setTabText(1, tr("诊断"));
    }
    updateDiagnosticsIssues();
    updateProblemShortcut();
}

QString DesktopRunPanel::stateText(
    DesktopRunSessionCoordinatorState state) const
{
    switch (state)
    {
    case DesktopRunSessionCoordinatorState::Idle:
        return tr("空闲");
    case DesktopRunSessionCoordinatorState::Preparing:
        return tr("正在准备");
    case DesktopRunSessionCoordinatorState::Starting:
        return tr("正在启动");
    case DesktopRunSessionCoordinatorState::Running:
        return tr("正在运行");
    case DesktopRunSessionCoordinatorState::Stopping:
        return tr("正在停止");
    case DesktopRunSessionCoordinatorState::Terminal:
        return tr("已结束");
    }
    return tr("未知");
}

QString DesktopRunPanel::outcomeText(
    DesktopRunSessionCoordinatorOutcome outcome) const
{
    switch (outcome)
    {
    case DesktopRunSessionCoordinatorOutcome::None:
        return tr("无结果");
    case DesktopRunSessionCoordinatorOutcome::Succeeded:
        return tr("运行成功");
    case DesktopRunSessionCoordinatorOutcome::
        WorkspaceCreationFailed:
        return tr("会话准备失败");
    case DesktopRunSessionCoordinatorOutcome::FailedToStart:
        return tr("游戏启动失败");
    case DesktopRunSessionCoordinatorOutcome::NonZeroExit:
        return tr("游戏异常退出");
    case DesktopRunSessionCoordinatorOutcome::Crashed:
        return tr("游戏已崩溃");
    case DesktopRunSessionCoordinatorOutcome::StoppedByUser:
        return tr("已由用户停止");
    }
    return tr("未知结果");
}

void DesktopRunPanel::appendOutput(
    const QString& channel,
    const QString& text)
{
    if (text.isEmpty())
        return;
    constexpr qsizetype MaximumInputCharacters =
        256 * 1024;
    constexpr qsizetype MaximumPendingCharacters =
        64 * 1024;
    constexpr qsizetype MaximumLinesPerChunk = 4096;
    const bool inputTruncated =
        text.size() > MaximumInputCharacters;
    QString normalized =
        text.left(MaximumInputCharacters);
    normalized.replace(
        QStringLiteral("\r\n"),
        QStringLiteral("\n"));
    normalized.replace(
        QLatin1Char('\r'),
        QLatin1Char('\n'));
    QString& pending =
        channel == QStringLiteral("stderr")
        ? pendingStandardError
        : pendingStandardOutput;
    pending += normalized;
    const QString prefix =
        QStringLiteral("[%1] ").arg(channel);
    qsizetype newline = pending.indexOf(
        QLatin1Char('\n'));
    qsizetype emittedLines = 0;
    while (newline >= 0 &&
           emittedLines < MaximumLinesPerChunk)
    {
        outputEdit->appendPlainText(
            prefix + pending.left(newline));
        pending.remove(0, newline + 1);
        ++emittedLines;
        newline = pending.indexOf(
            QLatin1Char('\n'));
    }
    bool outputTruncated = inputTruncated;
    if (newline >= 0)
    {
        pending.clear();
        outputTruncated = true;
    }
    else if (pending.size() >
             MaximumPendingCharacters)
    {
        outputEdit->appendPlainText(
            prefix + pending.left(
                MaximumPendingCharacters));
        pending.clear();
        outputTruncated = true;
    }
    if (outputTruncated)
    {
        outputEdit->appendPlainText(
            prefix +
            tr("[编辑器已截断此段输出]"));
    }
}

void DesktopRunPanel::flushPendingOutput()
{
    if (!pendingStandardOutput.isEmpty())
    {
        outputEdit->appendPlainText(
            QStringLiteral("[stdout] ") +
            pendingStandardOutput);
        pendingStandardOutput.clear();
    }
    if (!pendingStandardError.isEmpty())
    {
        outputEdit->appendPlainText(
            QStringLiteral("[stderr] ") +
            pendingStandardError);
        pendingStandardError.clear();
    }
}
