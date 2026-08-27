#pragma once

#include "DesktopRunController.h"
#include "DesktopRunSessionCleanup.h"
#include "DesktopRunSessionWorkspace.h"
#include "SavedSceneLaunchPreparation.h"

#include <QObject>

#include <atomic>
#include <memory>
#include <optional>

class QThread;

enum class DesktopRunSessionCoordinatorState
{
    Idle,
    Preparing,
    Starting,
    Running,
    Stopping,
    Terminal
};

enum class DesktopRunSessionCoordinatorOutcome
{
    None,
    Succeeded,
    WorkspaceCreationFailed,
    FailedToStart,
    NonZeroExit,
    Crashed,
    StoppedByUser
};

struct DesktopRunSessionRequest
{
    QString executablePath;
    QString trustedSessionsBaseDirectory;
    PreparedSavedSceneLaunch preparedLaunch;
    DesktopRunSessionWorkspaceLimits limits;
};

struct DesktopRunSessionTerminalResult
{
    DesktopRunSessionCoordinatorOutcome outcome =
        DesktopRunSessionCoordinatorOutcome::None;
    DesktopRunOutcome processOutcome = DesktopRunOutcome::None;
    int exitCode = 0;
    bool forcedKill = false;
    QString sessionId;
    QString sessionPath;
    QString message;
    DesktopRunSessionWorkspaceResult workspaceResult;

    bool succeeded() const
    {
        return outcome ==
            DesktopRunSessionCoordinatorOutcome::Succeeded;
    }
};

struct DesktopRunSessionPresentation
{
    QString sessionId;
    EditorRun::TargetKind targetKind =
        EditorRun::TargetKind::Scene;
    QString sceneId;
    QString sceneName;
    QString mapPath;
    QString entryScriptPath;
    QString activeResourcePackId;
    DesktopRunSessionPaths paths;
};

// Owns exactly one editor/game run at a time. The finished session stays
// available only inside this editor process so its diagnostics and trace can be
// inspected. Starting another run or closing the editor removes it.
class DesktopRunSessionCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit DesktopRunSessionCoordinator(
        QObject* parent = nullptr);
    ~DesktopRunSessionCoordinator() override;

    DesktopRunSessionCoordinatorState state() const;
    bool isActive() const;
    bool hasCurrentSession() const;
    bool start(const DesktopRunSessionRequest& request);
    void requestStop();
    void prepareForApplicationExit();
    void setStopTimeoutMilliseconds(int milliseconds);

    DesktopRunSessionTerminalResult terminalResult() const;
    std::optional<DesktopRunSessionPresentation>
    currentSessionPresentation() const;

signals:
    void stateChanged(DesktopRunSessionCoordinatorState state);
    void standardOutputReceived(const QString& text);
    void standardErrorReceived(const QString& text);
    void sessionPrepared(
        const DesktopRunSessionPresentation& session);
    void terminalResultChanged(
        const DesktopRunSessionTerminalResult& result);

private:
    void setState(DesktopRunSessionCoordinatorState state);
    void handleWorkspacePrepared(
        quint64 generation,
        DesktopRunSessionWorkspaceResult result);
    void handleProcessStateChanged(DesktopRunState processState);
    void handleProcessFinished(
        DesktopRunOutcome outcome,
        int exitCode,
        bool forcedKill);
    void publishProcessTerminalResult(
        DesktopRunOutcome processOutcome,
        int exitCode,
        bool forcedKill,
        const QString& startFailureMessage = {});
    void publishTerminalResult(
        DesktopRunSessionCoordinatorOutcome outcome,
        DesktopRunOutcome processOutcome,
        int exitCode,
        bool forcedKill,
        const QString& message);
    bool cleanupWorkspace(
        const DesktopRunSessionWorkspace& workspace,
        const QString& trustedSessionsBaseDirectory,
        QString* failureMessage = nullptr);
    bool cleanupCurrentSession(QString* failureMessage = nullptr);
    void releaseBackgroundThreadForDestruction();

    DesktopRunController processController;
    DesktopRunSessionCoordinatorState currentState =
        DesktopRunSessionCoordinatorState::Idle;
    DesktopRunSessionRequest currentRequest;
    DesktopRunSessionWorkspaceResult currentWorkspaceResult;
    std::optional<DesktopRunSessionWorkspace> currentWorkspace;
    DesktopRunSessionTerminalResult currentTerminalResult;
    std::optional<DesktopRunSessionPresentation>
        currentPresentation;
    QThread* backgroundThread = nullptr;
    std::shared_ptr<DesktopRunSessionWorkspaceResult>
        backgroundWorkspaceResult;
    std::shared_ptr<std::atomic_bool> cancellationFlag;
    quint64 currentGeneration = 0;
    bool processFinishedHandled = false;
};
