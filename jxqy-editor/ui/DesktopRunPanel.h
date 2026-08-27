#pragma once

#include "../core/DesktopRunSessionCoordinator.h"
#include "../core/ProjectRuntimeConfiguration.h"

#include <QWidget>

class QComboBox;
class QEvent;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableView;
class QTabWidget;
class QTimer;
class QToolButton;

class DesktopRunDiagnosticsModel;

class DesktopRunPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit DesktopRunPanel(QWidget* parent = nullptr);

    void setProjectRuntimeConfiguration(
        const ProjectRuntimeConfiguration& configuration,
        bool projectAvailable);
    void setActiveResourcePackId(
        const QString& resourcePackId);
    void setDesktopExecutable(
        const QString& executablePath,
        bool valid);
    void setPlaytestTargetText(const QString& text);
    void setAuthoringOperationInProgress(bool inProgress);
    void setSessionCleanupInProgress(bool inProgress);
    QString selectedSceneId() const;

    void setCoordinatorState(
        DesktopRunSessionCoordinatorState state);
    void setSession(
        const DesktopRunSessionPresentation& session,
        bool diagnosticsProducerActive);
    void appendStandardOutput(const QString& text);
    void appendStandardError(const QString& text);
    void setTerminalResult(
        const DesktopRunSessionTerminalResult& result);
    bool detailsExpanded() const;
    void clearSessionPresentation();

    DesktopRunDiagnosticsModel* diagnosticsModel() const;

signals:
    void playtestRequested();
    void stopRequested();
    void desktopExecutableSelectionRequested();
    void detailsExpandedChanged(bool expanded);
    void sourceLocationRequested(
        const QString& sessionId,
        const QString& file,
        quint32 line,
        quint32 column);

protected:
    void changeEvent(QEvent* event) override;

private:
    void updateButtonStates();
    void updateDiagnosticsIssues();
    void updateProblemShortcut();
    void retranslateUi();
    QString stateText(
        DesktopRunSessionCoordinatorState state) const;
    QString outcomeText(
        DesktopRunSessionCoordinatorOutcome outcome) const;
    void appendOutput(
        const QString& channel,
        const QString& text);
    void flushPendingOutput();

    QFormLayout* detailsFormLayout = nullptr;
    QToolButton* detailsToggleButton = nullptr;
    QWidget* detailsWidget = nullptr;
    QLabel* targetCaptionLabel = nullptr;
    QLabel* playtestTargetValueLabel = nullptr;
    QLabel* stateCaptionLabel = nullptr;
    QLabel* resultCaptionLabel = nullptr;
    QComboBox* sceneComboBox = nullptr;
    QLabel* activePackValueLabel = nullptr;
    QLabel* sessionTargetValueLabel = nullptr;
    QLabel* sessionPackValueLabel = nullptr;
    QLabel* executableValueLabel = nullptr;
    QPushButton* chooseExecutableButton = nullptr;
    QLabel* sessionValueLabel = nullptr;
    QLabel* overlayValueLabel = nullptr;
    QLabel* stateValueLabel = nullptr;
    QLabel* progressValueLabel = nullptr;
    QLabel* exitValueLabel = nullptr;
    QPushButton* runButton = nullptr;
    QPushButton* stopButton = nullptr;
    QToolButton* problemButton = nullptr;
    QPlainTextEdit* outputEdit = nullptr;
    QTabWidget* outputTabs = nullptr;
    QTableView* diagnosticsView = nullptr;
    QLabel* diagnosticsIssueLabel = nullptr;
    DesktopRunDiagnosticsModel* diagnostics = nullptr;
    QTimer* diagnosticsRefreshTimer = nullptr;

    ProjectRuntimeConfiguration currentConfiguration;
    bool currentProjectAvailable = false;
    bool currentExecutableValid = false;
    bool currentAuthoringOperationInProgress = false;
    bool currentSessionCleanupInProgress = false;
    QString currentPlaytestTargetText;
    QString currentExecutablePath;
    QString currentResourcePackId;
    QString currentSessionId;
    QString currentSessionPath;
    QString currentSessionTarget;
    QString currentSessionResourcePackId;
    QString currentOverlayPath;
    QString pendingStandardOutput;
    QString pendingStandardError;
    DesktopRunSessionCoordinatorState currentState =
        DesktopRunSessionCoordinatorState::Idle;
};
