#include "../ui/DesktopRunDiagnosticsModel.h"
#include "../ui/DesktopRunPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <iostream>

namespace
{
const QString SessionId =
    QStringLiteral("11111111-2222-4333-8444-555555555555");

bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

QString truncatedOutputMarker()
{
    return QStringLiteral("[stdout] ") +
        QCoreApplication::translate(
            "DesktopRunPanel",
            "[编辑器已截断此段输出]");
}

template <typename Widget>
Widget* requiredChild(
    DesktopRunPanel& panel,
    const char* objectName,
    bool& ok)
{
    Widget* child = panel.findChild<Widget*>(
        QString::fromLatin1(objectName));
    ok = check(child != nullptr, objectName) && ok;
    return child;
}

ProjectRuntimeConfiguration makeConfiguration()
{
    ProjectScene first;
    first.id = QStringLiteral("opening");
    first.name = QStringLiteral("Opening");

    ProjectScene second;
    second.id = QStringLiteral("courtyard");
    second.name = QStringLiteral("Courtyard");

    ProjectRuntimeConfiguration configuration;
    configuration.defaultSceneId = second.id;
    configuration.scenes = {first, second};
    return configuration;
}

bool writeFile(
    const QString& path,
    const QByteArray& bytes)
{
    QFile file(path);
    return file.open(
               QIODevice::WriteOnly |
               QIODevice::Truncate) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

bool hasDiagnosticsIssue(
    const DesktopRunDiagnosticsModel& model,
    DesktopRunDiagnosticsModel::IssueCode code)
{
    for (const DesktopRunDiagnosticsModel::Issue& issue :
         model.issues())
    {
        if (issue.code == code)
            return true;
    }
    return false;
}

bool testCompactPresentationAndFriendlyIdentifiers()
{
    bool ok = true;
    DesktopRunPanel panel;
    QWidget* detailsWidget =
        requiredChild<QWidget>(
            panel,
            "desktopRunDetailsWidget",
            ok);
    QToolButton* detailsToggleButton =
        requiredChild<QToolButton>(
            panel,
            "desktopRunDetailsToggleButton",
            ok);
    QComboBox* sceneComboBox =
        requiredChild<QComboBox>(
            panel,
            "desktopRunSceneComboBox",
            ok);
    QLabel* targetLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunTargetValueLabel",
            ok);
    QLabel* executableLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunExecutableValueLabel",
            ok);
    QLabel* stateLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunStateValueLabel",
            ok);
    QLabel* resultLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunExitValueLabel",
            ok);
    QLabel* sessionLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunSessionValueLabel",
            ok);
    QLabel* overlayLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunOverlayValueLabel",
            ok);
    QLabel* activePackLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunActivePackValueLabel",
            ok);
    QLabel* sessionTargetLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunSessionTargetValueLabel",
            ok);
    QLabel* progressLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunProgressValueLabel",
            ok);
    QPushButton* runButton =
        requiredChild<QPushButton>(
            panel,
            "desktopRunStartButton",
            ok);
    QPushButton* stopButton =
        requiredChild<QPushButton>(
            panel,
            "desktopRunStopButton",
            ok);
    QToolButton* problemButton =
        requiredChild<QToolButton>(
            panel,
            "desktopRunProblemButton",
            ok);
    QTabWidget* outputTabs =
        requiredChild<QTabWidget>(
            panel,
            "desktopRunOutputTabs",
            ok);
    if (!detailsWidget || !detailsToggleButton ||
        !sceneComboBox || !targetLabel ||
        !executableLabel ||
        !stateLabel || !resultLabel || !sessionLabel ||
        !overlayLabel || !activePackLabel ||
        !sessionTargetLabel || !progressLabel ||
        !runButton || !stopButton || !problemButton ||
        !outputTabs)
    {
        return false;
    }

    ok = check(
        detailsWidget->isHidden() &&
        detailsToggleButton->isCheckable() &&
        !detailsWidget->isAncestorOf(targetLabel) &&
        detailsWidget->isAncestorOf(sceneComboBox) &&
        detailsWidget->isAncestorOf(executableLabel) &&
        !detailsWidget->isAncestorOf(stateLabel) &&
        !detailsWidget->isAncestorOf(resultLabel) &&
        !detailsWidget->isAncestorOf(runButton) &&
        !detailsWidget->isAncestorOf(stopButton) &&
        detailsWidget->isAncestorOf(activePackLabel) &&
        detailsWidget->isAncestorOf(sessionTargetLabel) &&
        detailsWidget->isAncestorOf(sessionLabel) &&
        detailsWidget->isAncestorOf(overlayLabel) &&
        detailsWidget->isAncestorOf(progressLabel) &&
        detailsWidget->isAncestorOf(outputTabs),
        "playtest starts with target, state, result, and actions while configuration and technical controls stay in details") &&
        ok;
    panel.setPlaytestTargetText(
        QString::fromUtf8("当前脚本：talk.txt"));
    ok = check(
        targetLabel->text() ==
            QString::fromUtf8("当前脚本：talk.txt") &&
        problemButton->isHidden(),
        "compact playtest target uses author-facing text and hides the problem shortcut when no problem exists") &&
        ok;
    detailsToggleButton->click();
    ok = check(
        detailsToggleButton->isChecked() &&
        !detailsWidget->isHidden() &&
        detailsToggleButton->arrowType() ==
            Qt::DownArrow,
        "details entry expands logs and secondary controls") &&
        ok;
    detailsToggleButton->click();
    ok = check(
        !detailsToggleButton->isChecked() &&
        detailsWidget->isHidden() &&
        detailsToggleButton->arrowType() ==
            Qt::RightArrow,
        "details entry returns to the compact presentation") &&
        ok;

    ProjectScene firstDuplicate;
    firstDuplicate.id =
        QStringLiteral("shared-prefix-west-1234");
    firstDuplicate.name = QStringLiteral("Village");
    ProjectScene secondDuplicate;
    secondDuplicate.id =
        QStringLiteral("shared-prefix-east-5678");
    secondDuplicate.name = QStringLiteral("Village");
    ProjectScene unique;
    unique.id = QStringLiteral("courtyard");
    unique.name = QStringLiteral("Courtyard");
    ProjectScene unnamed;
    unnamed.id =
        QStringLiteral(
            "33333333-3333-4333-8333-333333333333");
    ProjectRuntimeConfiguration configuration;
    configuration.scenes = {
        firstDuplicate,
        secondDuplicate,
        unique,
        unnamed
    };
    configuration.defaultSceneId = secondDuplicate.id;
    panel.setProjectRuntimeConfiguration(
        configuration,
        true);
    ok = check(
        sceneComboBox->itemText(0).startsWith(
            QStringLiteral("Village")) &&
        sceneComboBox->itemText(1).startsWith(
            QStringLiteral("Village")) &&
        sceneComboBox->itemText(0) !=
            sceneComboBox->itemText(1) &&
        !sceneComboBox->itemText(0).contains(
            firstDuplicate.id) &&
        !sceneComboBox->itemText(1).contains(
            secondDuplicate.id) &&
        !sceneComboBox->itemText(0).contains(
            firstDuplicate.id.left(8)) &&
        !sceneComboBox->itemText(1).contains(
            secondDuplicate.id.left(8)) &&
        sceneComboBox->itemText(0) ==
            QStringLiteral("Village (1)") &&
        sceneComboBox->itemText(1) ==
            QStringLiteral("Village (2)") &&
        sceneComboBox->itemText(2) ==
            QStringLiteral("Courtyard") &&
        !sceneComboBox->itemText(3).isEmpty() &&
        !sceneComboBox->itemText(3).contains(
            unnamed.id) &&
        !sceneComboBox->itemText(3).contains(
            unnamed.id.left(8)) &&
        sceneComboBox->itemData(0).toString() ==
            firstDuplicate.id &&
        sceneComboBox->itemData(1).toString() ==
            secondDuplicate.id &&
        sceneComboBox->itemData(3).toString() ==
            unnamed.id &&
        sceneComboBox->itemData(
            0,
            Qt::ToolTipRole).toString().contains(
                firstDuplicate.id) &&
        panel.selectedSceneId() == secondDuplicate.id,
        "duplicate scene labels use friendly ordinals and retain exact IDs only as data and tooltips") &&
        ok;

    const QString executablePath =
        QStringLiteral(
            "C:/very/long/editor/run/path/"
            "jxqy-all-in-one-debug.exe");
    panel.setDesktopExecutable(executablePath, true);
    DesktopRunSessionPresentation session;
    session.sessionId = SessionId;
    session.sceneId = secondDuplicate.id;
    session.sceneName = secondDuplicate.name;
    session.paths.sessionRoot =
        QStringLiteral(
            "C:/very/long/session/root/") +
        SessionId;
    session.paths.overlayRoot =
        session.paths.sessionRoot +
        QStringLiteral("/overlay");
    panel.setSession(session, false);
    ok = check(
        executableLabel->text() ==
            QStringLiteral(
                "jxqy-all-in-one-debug.exe") &&
        executableLabel->toolTip() == executablePath &&
        !executableLabel->wordWrap() &&
        !sessionLabel->wordWrap() &&
        !overlayLabel->wordWrap() &&
        sessionLabel->toolTip() ==
            session.paths.sessionRoot &&
        overlayLabel->toolTip() ==
            session.paths.overlayRoot &&
        sessionTargetLabel->text() ==
            secondDuplicate.name &&
        !sessionTargetLabel->text().contains(
            secondDuplicate.id) &&
        sessionTargetLabel->toolTip().contains(
            secondDuplicate.id),
        "long paths and scene IDs stay out of visible labels while tooltips preserve their complete values") &&
        ok;

    session.sceneId = unnamed.id;
    session.sceneName.clear();
    panel.setSession(session, false);
    ok = check(
        sessionTargetLabel->text() ==
            QString::fromUtf8("未命名场景") &&
        !sessionTargetLabel->text().contains(
            unnamed.id) &&
        sessionTargetLabel->toolTip().contains(
            unnamed.id),
        "unnamed scene sessions use a friendly label and keep the exact ID in the tooltip") &&
        ok;

    return ok;
}

bool testConfigurationAndButtonSignals()
{
    bool ok = true;
    DesktopRunPanel panel;
    QComboBox* sceneComboBox =
        requiredChild<QComboBox>(
            panel,
            "desktopRunSceneComboBox",
            ok);
    QPushButton* runButton =
        requiredChild<QPushButton>(
            panel,
            "desktopRunStartButton",
            ok);
    QPushButton* stopButton =
        requiredChild<QPushButton>(
            panel,
            "desktopRunStopButton",
            ok);
    QPushButton* chooseExecutableButton =
        requiredChild<QPushButton>(
            panel,
            "desktopRunChooseExecutableButton",
            ok);
    QLabel* activePackLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunActivePackValueLabel",
            ok);
    QLabel* progressLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunProgressValueLabel",
            ok);
    if (!sceneComboBox || !runButton || !stopButton ||
        !chooseExecutableButton || !activePackLabel ||
        !progressLabel)
    {
        return false;
    }

    ok = check(
        !runButton->isEnabled() &&
        !stopButton->isEnabled() &&
        !sceneComboBox->isEnabled() &&
        chooseExecutableButton->isEnabled(),
        "initial button state is safe") && ok;

    const ProjectRuntimeConfiguration configuration =
        makeConfiguration();
    panel.setProjectRuntimeConfiguration(
        configuration,
        true);
    ok = check(
        panel.selectedSceneId() ==
            QStringLiteral("courtyard") &&
        sceneComboBox->isEnabled() &&
        runButton->isEnabled(),
        "opening a project enables playtest guidance even before the executable is valid") &&
        ok;

    panel.setActiveResourcePackId(
        QStringLiteral("chapter-one"));
    panel.setDesktopExecutable(
        QStringLiteral("C:/game/jxqy.exe"),
        true);
    ok = check(
        activePackLabel->text() ==
            QStringLiteral("chapter-one") &&
        runButton->isEnabled(),
        "valid project context enables run") && ok;

    panel.setAuthoringOperationInProgress(true);
    ok = check(
        !runButton->isEnabled() &&
        !sceneComboBox->isEnabled() &&
        !chooseExecutableButton->isEnabled(),
        "an authoring transaction disables run configuration") &&
        ok;
    panel.setAuthoringOperationInProgress(false);
    ok = check(
        runButton->isEnabled() &&
        sceneComboBox->isEnabled() &&
        chooseExecutableButton->isEnabled(),
        "finishing an authoring transaction restores idle controls") &&
        ok;

    panel.setSessionCleanupInProgress(true);
    ok = check(
        !runButton->isEnabled() &&
        !stopButton->isEnabled() &&
        !sceneComboBox->isEnabled() &&
        !chooseExecutableButton->isEnabled() &&
        progressLabel->text().contains(
            QString::fromUtf8("正在清理")) &&
        panel.cursor().shape() == Qt::WaitCursor,
        "temporary-session cleanup is visible and locks run configuration") &&
        ok;
    panel.setSessionCleanupInProgress(false);
    ok = check(
        runButton->isEnabled() &&
        sceneComboBox->isEnabled() &&
        chooseExecutableButton->isEnabled() &&
        progressLabel->text().isEmpty(),
        "finishing temporary-session cleanup restores idle controls") &&
        ok;

    int runRequestCount = 0;
    int stopRequestCount = 0;
    int executableSelectionRequestCount = 0;
    QObject::connect(
        &panel,
        &DesktopRunPanel::playtestRequested,
        &panel,
        [&]()
        {
            ++runRequestCount;
        });
    QObject::connect(
        &panel,
        &DesktopRunPanel::stopRequested,
        &panel,
        [&]()
        {
            ++stopRequestCount;
        });
    QObject::connect(
        &panel,
        &DesktopRunPanel::
            desktopExecutableSelectionRequested,
        &panel,
        [&]()
        {
            ++executableSelectionRequestCount;
        });
    runButton->click();
    chooseExecutableButton->click();
    ok = check(
        runRequestCount == 1 &&
        executableSelectionRequestCount == 1,
        "playtest and executable-selection actions emit their unified requests") &&
        ok;

    const DesktopRunSessionCoordinatorState stoppableStates[] = {
        DesktopRunSessionCoordinatorState::Preparing,
        DesktopRunSessionCoordinatorState::Starting,
        DesktopRunSessionCoordinatorState::Running
    };
    for (DesktopRunSessionCoordinatorState state :
         stoppableStates)
    {
        panel.setCoordinatorState(state);
        ok = check(
            !runButton->isEnabled() &&
            stopButton->isEnabled() &&
            !sceneComboBox->isEnabled() &&
            !chooseExecutableButton->isEnabled(),
            "preparing, starting, and running states are stoppable") &&
            ok;
    }
    stopButton->click();
    ok = check(
        stopRequestCount == 1,
        "enabled stop button forwards one request") && ok;

    panel.setCoordinatorState(
        DesktopRunSessionCoordinatorState::Stopping);
    ok = check(
             !runButton->isEnabled() &&
                 !stopButton->isEnabled() &&
                 !sceneComboBox->isEnabled() &&
                 !chooseExecutableButton->isEnabled(),
             "stopping locks active controls") &&
        ok;

    panel.setCoordinatorState(
        DesktopRunSessionCoordinatorState::Idle);
    panel.setProjectRuntimeConfiguration(
        configuration,
        false);
    ok = check(
        !runButton->isEnabled() &&
        !sceneComboBox->isEnabled() &&
        chooseExecutableButton->isEnabled(),
        "closing the project disables scene launch") && ok;
    return ok;
}

bool testSessionTerminalOutput()
{
    bool ok = true;
    DesktopRunPanel panel;
    QPushButton* runButton =
        requiredChild<QPushButton>(
            panel,
            "desktopRunStartButton",
            ok);
    QLabel* sessionLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunSessionValueLabel",
            ok);
    QLabel* sessionTargetLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunSessionTargetValueLabel",
            ok);
    QLabel* sessionPackLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunSessionPackValueLabel",
            ok);
    QLabel* overlayLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunOverlayValueLabel",
            ok);
    QLabel* resultLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunExitValueLabel",
            ok);
    QPlainTextEdit* outputEdit =
        requiredChild<QPlainTextEdit>(
            panel,
            "desktopRunOutputEdit",
            ok);
    if (!runButton || !sessionLabel ||
        !sessionTargetLabel || !sessionPackLabel ||
        !overlayLabel || !resultLabel || !outputEdit)
    {
        return false;
    }

    panel.setProjectRuntimeConfiguration(
        makeConfiguration(),
        true);
    panel.setDesktopExecutable(
        QStringLiteral("C:/game/jxqy.exe"),
        true);
    panel.setCoordinatorState(
        DesktopRunSessionCoordinatorState::Preparing);

    DesktopRunSessionPresentation session;
    session.sessionId = SessionId;
    session.sceneId = QStringLiteral("courtyard");
    session.sceneName = QStringLiteral("Courtyard");
    session.mapPath = QStringLiteral("map/courtyard.map");
    session.entryScriptPath =
        QStringLiteral("script/courtyard.lua");
    session.activeResourcePackId =
        QStringLiteral("chapter-one");
    session.paths.sessionRoot =
        QStringLiteral("C:/sessions/") + SessionId;
    session.paths.overlayRoot =
        session.paths.sessionRoot +
        QStringLiteral("/overlay");
    panel.setSession(session, true);
    panel.setCoordinatorState(
        DesktopRunSessionCoordinatorState::Running);
    ok = check(
        sessionLabel->text() == session.paths.sessionRoot &&
        sessionLabel->toolTip() == session.paths.sessionRoot &&
        sessionTargetLabel->text() ==
            QStringLiteral("Courtyard") &&
        sessionTargetLabel->toolTip().contains(
            QStringLiteral("courtyard")) &&
        sessionPackLabel->text() ==
            QStringLiteral("chapter-one") &&
        overlayLabel->text() == session.paths.overlayRoot,
        "prepared target, pack, and paths are visible") &&
        ok;

    session.targetKind = EditorRun::TargetKind::Map;
    panel.setSession(session, true);
    ok = check(
        sessionTargetLabel->text() ==
            QString::fromUtf8(
                "当前地图：map/courtyard.map"),
        "a current-map session exposes the actual map target path") &&
        ok;
    session.targetKind = EditorRun::TargetKind::Script;
    panel.setSession(session, true);
    ok = check(
        sessionTargetLabel->text() ==
            QString::fromUtf8(
                "当前脚本：script/courtyard.lua"),
        "a current-script session exposes the actual script target path") &&
        ok;
    session.targetKind = EditorRun::TargetKind::Scene;
    panel.setSession(session, true);

    constexpr qsizetype PendingOutputBoundary =
        64 * 1024;
    const QString longPartial(
        PendingOutputBoundary + 3,
        QLatin1Char('x'));
    panel.appendStandardOutput(longPartial);
    panel.appendStandardError(
        QStringLiteral("failure-tail"));
    ok = check(
        outputEdit->maximumBlockCount() == 10000 &&
        outputEdit->document()->blockCount() == 2 &&
        outputEdit->toPlainText().startsWith(
            QStringLiteral("[stdout] ") +
            QString(
                PendingOutputBoundary,
                QLatin1Char('x'))) &&
        outputEdit->toPlainText().endsWith(
            truncatedOutputMarker()),
        "output is bounded and reports a truncated pending chunk") &&
        ok;

    DesktopRunSessionTerminalResult result;
    result.outcome =
        DesktopRunSessionCoordinatorOutcome::NonZeroExit;
    result.processOutcome = DesktopRunOutcome::NonZeroExit;
    result.exitCode = 70;
    result.sessionId = SessionId;
    result.sessionPath = session.paths.sessionRoot;
    result.message = QStringLiteral("script failed");
    panel.setCoordinatorState(
        DesktopRunSessionCoordinatorState::Terminal);
    panel.setTerminalResult(result);

    const QString output = outputEdit->toPlainText();
    ok = check(
        runButton->isEnabled() &&
        outputEdit->document()->blockCount() == 3 &&
        output.startsWith(
            QStringLiteral("[stdout] ") +
            QString(
                PendingOutputBoundary,
                QLatin1Char('x'))) &&
        output.contains(
            QLatin1Char('\n') +
            truncatedOutputMarker() +
            QLatin1Char('\n')) &&
        output.endsWith(
            QStringLiteral("[stderr] failure-tail")),
        "terminal result preserves the output limit evidence and flushes stderr") &&
        ok;
    ok = check(
        !resultLabel->text().contains(QStringLiteral("70")) &&
        !resultLabel->text().contains(
            QStringLiteral("script failed")) &&
        resultLabel->toolTip().contains(
            QStringLiteral("70")) &&
        resultLabel->toolTip().contains(
            QStringLiteral("script failed")),
        "compact terminal result hides the technical message and exit code while retaining details") && ok;

    panel.clearSessionPresentation();
    ok = check(
             sessionLabel->text() ==
                     QString::fromUtf8("尚未创建") &&
                 sessionTargetLabel->text() ==
                     QString::fromUtf8("尚未创建") &&
                 sessionPackLabel->text() ==
                     QString::fromUtf8("尚未创建") &&
                 overlayLabel->text() ==
                     QString::fromUtf8("尚未创建"),
             "clearing the current session removes its presentation") &&
        ok;
    return ok;
}

bool testDiagnosticsRefreshAndSourceForwarding()
{
    bool ok = true;
    QTemporaryDir temporaryDirectory;
    ok = check(
        temporaryDirectory.isValid(),
        "create diagnostics temporary directory") && ok;
    if (!temporaryDirectory.isValid())
        return false;

    const QString diagnosticsPath =
        temporaryDirectory.filePath(
            QStringLiteral("diagnostics.jsonl"));
    const QByteArray diagnosticsLine =
        QByteArrayLiteral(
            "{\"schemaVersion\":1,"
            "\"sessionId\":\"11111111-2222-4333-8444-555555555555\","
            "\"sequence\":1,"
            "\"severity\":\"error\","
            "\"code\":\"editor_run.script.runtime_failed\","
            "\"message\":\"script failed\","
            "\"file\":\"script/chapter/entry.lua\","
            "\"line\":12,"
            "\"column\":4,"
            "\"target\":\"courtyard\"}\n");
    ok = check(
        writeFile(diagnosticsPath, diagnosticsLine),
        "write diagnostics JSONL fixture") && ok;
    if (!ok)
        return false;

    DesktopRunPanel panel;
    QTableView* diagnosticsView =
        requiredChild<QTableView>(
            panel,
            "desktopRunDiagnosticsView",
            ok);
    QLabel* diagnosticsIssueLabel =
        requiredChild<QLabel>(
            panel,
            "desktopRunDiagnosticsIssueLabel",
            ok);
    QToolButton* problemButton =
        requiredChild<QToolButton>(
            panel,
            "desktopRunProblemButton",
            ok);
    if (!diagnosticsView || !diagnosticsIssueLabel ||
        !problemButton)
        return false;

    DesktopRunSessionPresentation session;
    session.sessionId = SessionId;
    session.paths.sessionRoot =
        temporaryDirectory.filePath(
            QStringLiteral("session"));
    session.paths.diagnosticsPath = diagnosticsPath;
    panel.setSession(session, true);
    ok = check(
        panel.diagnosticsModel()->diagnosticsPath() ==
            diagnosticsPath &&
        panel.diagnosticsModel()->sessionId() == SessionId &&
        panel.diagnosticsModel()->refresh() &&
        panel.diagnosticsModel()->rowCount() == 1 &&
        diagnosticsIssueLabel->text().isEmpty() &&
        !problemButton->isHidden() &&
        problemButton->text().contains(
            QString::fromUtf8("转到问题")),
        "panel diagnostics refresh exposes a compact problem shortcut for a source-bound error") &&
        ok;

    QString sourceSessionId;
    QString sourceFile;
    quint32 sourceLine = 0;
    quint32 sourceColumn = 0;
    int sourceRequestCount = 0;
    QObject::connect(
        &panel,
        &DesktopRunPanel::sourceLocationRequested,
        &panel,
        [&](const QString& sessionId,
            const QString& file,
            quint32 line,
            quint32 column)
        {
            ++sourceRequestCount;
            sourceSessionId = sessionId;
            sourceFile = file;
            sourceLine = line;
            sourceColumn = column;
        });

    const QModelIndex sourceIndex =
        panel.diagnosticsModel()->index(
            0,
            DesktopRunDiagnosticsModel::LocationColumn);
    problemButton->click();
    ok = check(
        sourceRequestCount == 1 &&
        sourceSessionId == SessionId &&
        sourceFile ==
            QStringLiteral("script/chapter/entry.lua") &&
        sourceLine == 12 &&
        sourceColumn == 4,
        "compact problem shortcut returns directly to the first source-bound problem") &&
        ok;
    Q_EMIT diagnosticsView->doubleClicked(sourceIndex);
    ok = check(
        sourceRequestCount == 2 &&
        sourceSessionId == SessionId &&
        sourceFile ==
            QStringLiteral("script/chapter/entry.lua") &&
        sourceLine == 12 &&
        sourceColumn == 4,
        "diagnostic row activation still forwards the exact source location from details") &&
        ok;

    panel.setCoordinatorState(
        DesktopRunSessionCoordinatorState::Preparing);
    ok = check(
        panel.diagnosticsModel()->diagnosticsPath().isEmpty() &&
        panel.diagnosticsModel()->sessionId().isEmpty() &&
        panel.diagnosticsModel()->rowCount() == 0 &&
        problemButton->isHidden(),
        "preparing a new run clears the previous diagnostics source") &&
        ok;
    Q_EMIT diagnosticsView->doubleClicked(sourceIndex);
    ok = check(
        sourceRequestCount == 2,
        "a stale diagnostics index cannot emit after source reset") &&
        ok;
    return ok;
}

bool testCurrentAndTerminalDiagnosticsFinalization()
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create terminal diagnostics temporary directory"))
    {
        return false;
    }

    const QString diagnosticsPath =
        temporaryDirectory.filePath(
            QStringLiteral(
                "terminal-diagnostics.jsonl"));
    const QByteArray diagnosticsBytes =
        QByteArrayLiteral(
            "{\"schemaVersion\":1,"
            "\"sessionId\":\"11111111-2222-4333-8444-555555555555\","
            "\"sequence\":1,"
            "\"severity\":\"error\","
            "\"code\":\"editor_run.script.runtime_failed\","
            "\"message\":\"script failed\"}\n"
            "{\"schemaVersion\":1");
    if (!check(
            writeFile(
                diagnosticsPath,
                diagnosticsBytes),
            "write complete plus terminal partial diagnostics fixture"))
    {
        return false;
    }

    DesktopRunSessionPresentation session;
    session.sessionId = SessionId;
    session.paths.sessionRoot =
        temporaryDirectory.filePath(
            QStringLiteral("session"));
    session.paths.diagnosticsPath =
        diagnosticsPath;

    bool ok = true;
    DesktopRunPanel livePanel;
    QTimer* liveTimer =
        requiredChild<QTimer>(
            livePanel,
            "desktopRunDiagnosticsRefreshTimer",
            ok);
    livePanel.setSession(session, true);
    ok = check(
        liveTimer &&
        liveTimer->isActive() &&
        !hasDiagnosticsIssue(
            *livePanel.diagnosticsModel(),
            DesktopRunDiagnosticsModel::
                IssueCode::IncompleteLine),
        "a current live diagnostics session polls without treating a partial line as terminal") &&
        ok;

    DesktopRunSessionTerminalResult terminal;
    terminal.sessionId = SessionId;
    terminal.sessionPath =
        session.paths.sessionRoot;
    livePanel.setTerminalResult(terminal);
    ok = check(
        liveTimer &&
        !liveTimer->isActive() &&
        livePanel.diagnosticsModel()->
            rowCount() == 1 &&
        hasDiagnosticsIssue(
            *livePanel.diagnosticsModel(),
            DesktopRunDiagnosticsModel::
                IssueCode::IncompleteLine),
        "terminal publication stops polling and finalizes the partial diagnostics line") &&
        ok;

    livePanel.setSession(session, false);
    ok = check(
        liveTimer &&
        !liveTimer->isActive() &&
        hasDiagnosticsIssue(
            *livePanel.diagnosticsModel(),
            DesktopRunDiagnosticsModel::
                IssueCode::IncompleteLine),
        "switching back to the terminal current session remains finalized and does not restart polling") &&
        ok;

    DesktopRunPanel finishedPanel;
    QTimer* finishedTimer =
        requiredChild<QTimer>(
            finishedPanel,
            "desktopRunDiagnosticsRefreshTimer",
            ok);
    finishedPanel.setSession(session, false);
    ok = check(
        finishedTimer &&
        !finishedTimer->isActive() &&
        finishedPanel.diagnosticsModel()->
            rowCount() == 1 &&
        hasDiagnosticsIssue(
            *finishedPanel.diagnosticsModel(),
            DesktopRunDiagnosticsModel::
                IssueCode::IncompleteLine),
        "a finished current session finalizes diagnostics without a permanent polling timer") &&
        ok;
    return ok;
}
}

int main(int argc, char* argv[])
{
#if !defined(Q_OS_WIN)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
        qEnvironmentVariableIsEmpty("DISPLAY"))
    {
        qputenv(
            "QT_QPA_PLATFORM",
            QByteArray("offscreen"));
    }
#endif

    QApplication application(argc, argv);
    bool ok = true;
    ok = testCompactPresentationAndFriendlyIdentifiers() &&
        ok;
    ok = testConfigurationAndButtonSignals() && ok;
    ok = testSessionTerminalOutput() && ok;
    ok = testDiagnosticsRefreshAndSourceForwarding() && ok;
    ok = testCurrentAndTerminalDiagnosticsFinalization() && ok;
    return ok ? 0 : 1;
}
