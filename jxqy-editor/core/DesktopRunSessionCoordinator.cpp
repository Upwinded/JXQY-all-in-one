#include "DesktopRunSessionCoordinator.h"

#include "EditorProcessLifecycle.h"
#include "EditorSettings.h"

#include <QThread>

#include <utility>

namespace
{
DesktopRunSessionCoordinatorOutcome processOutcomeToCoordinatorOutcome(
    DesktopRunOutcome outcome)
{
    switch (outcome)
    {
    case DesktopRunOutcome::Succeeded:
        return DesktopRunSessionCoordinatorOutcome::Succeeded;
    case DesktopRunOutcome::NonZeroExit:
        return DesktopRunSessionCoordinatorOutcome::NonZeroExit;
    case DesktopRunOutcome::Crashed:
        return DesktopRunSessionCoordinatorOutcome::Crashed;
    case DesktopRunOutcome::FailedToStart:
        return DesktopRunSessionCoordinatorOutcome::FailedToStart;
    case DesktopRunOutcome::StoppedByUser:
        return DesktopRunSessionCoordinatorOutcome::StoppedByUser;
    case DesktopRunOutcome::None:
    default:
        return DesktopRunSessionCoordinatorOutcome::FailedToStart;
    }
}

QString fallbackProcessMessage(
    DesktopRunOutcome outcome,
    int exitCode)
{
    switch (outcome)
    {
    case DesktopRunOutcome::Succeeded:
        return QStringLiteral("Desktop game exited successfully");
    case DesktopRunOutcome::NonZeroExit:
        return QStringLiteral(
            "Desktop game exited with code %1").arg(exitCode);
    case DesktopRunOutcome::Crashed:
        return QStringLiteral("Desktop game process crashed");
    case DesktopRunOutcome::FailedToStart:
        return QStringLiteral(
            "Desktop game process could not be started");
    case DesktopRunOutcome::StoppedByUser:
        return QStringLiteral(
            "Desktop game process was stopped by the user");
    case DesktopRunOutcome::None:
    default:
        return QStringLiteral(
            "Desktop game process did not start");
    }
}

bool stateIsActive(DesktopRunSessionCoordinatorState state)
{
    return state == DesktopRunSessionCoordinatorState::Preparing ||
        state == DesktopRunSessionCoordinatorState::Starting ||
        state == DesktopRunSessionCoordinatorState::Running ||
        state == DesktopRunSessionCoordinatorState::Stopping;
}

DesktopRunSessionPresentation presentationForWorkspace(
    const DesktopRunSessionWorkspace& workspace)
{
    DesktopRunSessionPresentation presentation;
    presentation.sessionId = workspace.sessionId;
    presentation.targetKind = workspace.descriptor.target.kind;
    presentation.sceneId = QString::fromUtf8(
        workspace.descriptor.target.sceneId.data(),
        static_cast<qsizetype>(
            workspace.descriptor.target.sceneId.size()));
    presentation.sceneName = QString::fromUtf8(
        workspace.descriptor.target.sceneName.data(),
        static_cast<qsizetype>(
            workspace.descriptor.target.sceneName.size()));
    presentation.mapPath = QString::fromUtf8(
        workspace.descriptor.target.mapPath.data(),
        static_cast<qsizetype>(
            workspace.descriptor.target.mapPath.size()));
    presentation.entryScriptPath = QString::fromUtf8(
        workspace.descriptor.target.entryScriptPath.data(),
        static_cast<qsizetype>(
            workspace.descriptor.target.entryScriptPath.size()));
    presentation.activeResourcePackId = QString::fromUtf8(
        workspace.descriptor.activeResourcePackId.data(),
        static_cast<qsizetype>(
            workspace.descriptor.activeResourcePackId.size()));
    presentation.paths = workspace.paths;
    return presentation;
}
}

DesktopRunSessionCoordinator::DesktopRunSessionCoordinator(
    QObject* parent)
    : QObject(parent)
{
    connect(
        &processController,
        &DesktopRunController::stateChanged,
        this,
        &DesktopRunSessionCoordinator::handleProcessStateChanged);
    connect(
        &processController,
        &DesktopRunController::standardOutputReceived,
        this,
        &DesktopRunSessionCoordinator::standardOutputReceived);
    connect(
        &processController,
        &DesktopRunController::standardErrorReceived,
        this,
        &DesktopRunSessionCoordinator::standardErrorReceived);
    connect(
        &processController,
        &DesktopRunController::runFinished,
        this,
        &DesktopRunSessionCoordinator::handleProcessFinished);
}

DesktopRunSessionCoordinator::~DesktopRunSessionCoordinator()
{
    disconnect(&processController, nullptr, this, nullptr);
    if (cancellationFlag)
        cancellationFlag->store(true);
    if (processController.stopImmediatelyForApplicationExit())
        cleanupCurrentSession();
    releaseBackgroundThreadForDestruction();
}

DesktopRunSessionCoordinatorState
DesktopRunSessionCoordinator::state() const
{
    return currentState;
}

bool DesktopRunSessionCoordinator::isActive() const
{
    return stateIsActive(currentState);
}

bool DesktopRunSessionCoordinator::hasCurrentSession() const
{
    return currentWorkspace.has_value();
}

bool DesktopRunSessionCoordinator::start(
    const DesktopRunSessionRequest& request)
{
    if (isActive() ||
        backgroundThread != nullptr ||
        request.executablePath.isEmpty() ||
        request.trustedSessionsBaseDirectory.isEmpty() ||
        !EditorSettings::validateDesktopGameExecutable(
             request.executablePath).succeeded())
    {
        return false;
    }

    QString cleanupFailure;
    if (!cleanupCurrentSession(&cleanupFailure))
    {
        emit standardErrorReceived(
            QStringLiteral("[session-cleanup] %1\n")
                .arg(cleanupFailure));
        return false;
    }

    ++currentGeneration;
    const quint64 generation = currentGeneration;
    currentRequest = request;
    currentWorkspaceResult = {};
    currentWorkspace.reset();
    currentPresentation.reset();
    currentTerminalResult = {};
    processFinishedHandled = false;
    cancellationFlag =
        std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> workerCancellation =
        cancellationFlag;

    auto result =
        std::make_shared<DesktopRunSessionWorkspaceResult>();
    backgroundWorkspaceResult = result;
    DesktopRunSessionWorkspaceControl control;
    control.cancellationRequested =
        [workerCancellation]()
        {
            return workerCancellation->load();
        };
    QThread* worker = QThread::create(
        [request, control, result]()
        {
            *result = createDesktopRunSessionWorkspace(
                request.trustedSessionsBaseDirectory,
                request.preparedLaunch,
                request.limits,
                control);
        });
    registerEditorBackgroundWorker(worker);
    backgroundThread = worker;
    connect(
        worker,
        &QThread::finished,
        this,
        [this, worker, generation, result]()
        {
            if (backgroundThread == worker)
            {
                backgroundThread = nullptr;
                backgroundWorkspaceResult.reset();
            }
            worker->deleteLater();
            handleWorkspacePrepared(
                generation,
                std::move(*result));
        });
    setState(DesktopRunSessionCoordinatorState::Preparing);
    worker->start();
    return true;
}

void DesktopRunSessionCoordinator::requestStop()
{
    switch (currentState)
    {
    case DesktopRunSessionCoordinatorState::Preparing:
        if (cancellationFlag)
            cancellationFlag->store(true);
        setState(DesktopRunSessionCoordinatorState::Stopping);
        break;
    case DesktopRunSessionCoordinatorState::Starting:
    case DesktopRunSessionCoordinatorState::Running:
        processController.requestStop();
        break;
    case DesktopRunSessionCoordinatorState::Idle:
    case DesktopRunSessionCoordinatorState::Stopping:
    case DesktopRunSessionCoordinatorState::Terminal:
        break;
    }
}

void DesktopRunSessionCoordinator::prepareForApplicationExit()
{
    if (cancellationFlag)
        cancellationFlag->store(true);
    if (processController.stopImmediatelyForApplicationExit())
        cleanupCurrentSession();
}

void DesktopRunSessionCoordinator::setStopTimeoutMilliseconds(
    int milliseconds)
{
    processController.setStopTimeoutMilliseconds(milliseconds);
}

DesktopRunSessionTerminalResult
DesktopRunSessionCoordinator::terminalResult() const
{
    return currentTerminalResult;
}

std::optional<DesktopRunSessionPresentation>
DesktopRunSessionCoordinator::currentSessionPresentation() const
{
    return currentPresentation;
}

void DesktopRunSessionCoordinator::setState(
    DesktopRunSessionCoordinatorState state)
{
    if (currentState == state)
        return;
    currentState = state;
    emit stateChanged(currentState);
}

void DesktopRunSessionCoordinator::handleWorkspacePrepared(
    quint64 generation,
    DesktopRunSessionWorkspaceResult result)
{
    if (generation != currentGeneration)
        return;

    currentWorkspaceResult = result;
    if (!result.succeeded())
    {
        currentWorkspace.reset();
        currentTerminalResult = {};
        const bool cancelled =
            result.error ==
                DesktopRunSessionWorkspaceError::Cancelled;
        currentTerminalResult.outcome = cancelled
            ? DesktopRunSessionCoordinatorOutcome::StoppedByUser
            : DesktopRunSessionCoordinatorOutcome::
                  WorkspaceCreationFailed;
        currentTerminalResult.processOutcome = cancelled
            ? DesktopRunOutcome::StoppedByUser
            : DesktopRunOutcome::None;
        currentTerminalResult.exitCode = cancelled ? -1 : 0;
        currentTerminalResult.workspaceResult = std::move(result);
        currentTerminalResult.sessionId =
            currentTerminalResult.workspaceResult.sessionId;
        currentTerminalResult.sessionPath =
            currentTerminalResult.workspaceResult.partialFailureRoot;
        currentTerminalResult.message =
            currentTerminalResult.workspaceResult.message;

        if (currentTerminalResult.workspaceResult.cleanupWorkspace)
        {
            const DesktopRunSessionWorkspace cleanupCandidate =
                *currentTerminalResult.workspaceResult.cleanupWorkspace;
            QString cleanupFailure;
            if (cleanupWorkspace(
                    cleanupCandidate,
                    currentTerminalResult.workspaceResult.
                        trustedSessionsBaseDirectory,
                    &cleanupFailure))
            {
                currentTerminalResult.sessionPath.clear();
                currentTerminalResult.workspaceResult.
                    partialFailureRoot.clear();
            }
            else
            {
                currentWorkspace = cleanupCandidate;
                currentTerminalResult.message +=
                    QStringLiteral("; cleanup failed: ") +
                    cleanupFailure;
            }
        }
        cancellationFlag.reset();
        const DesktopRunSessionTerminalResult published =
            currentTerminalResult;
        setState(DesktopRunSessionCoordinatorState::Terminal);
        emit terminalResultChanged(published);
        return;
    }

    currentWorkspace = std::move(result.workspace.value());
    currentWorkspaceResult.workspace = currentWorkspace;
    const DesktopRunSessionPresentation presentation =
        presentationForWorkspace(*currentWorkspace);
    currentPresentation = presentation;
    emit sessionPrepared(presentation);
    const bool stoppedBeforeProcessStart =
        currentState == DesktopRunSessionCoordinatorState::Stopping ||
        (cancellationFlag && cancellationFlag->load());
    cancellationFlag.reset();
    if (stoppedBeforeProcessStart)
    {
        publishProcessTerminalResult(
            DesktopRunOutcome::StoppedByUser,
            -1,
            false);
        return;
    }
    const bool started = processController.start(
        currentRequest.executablePath,
        *currentWorkspace,
        currentWorkspace->paths.sessionRoot);
    if (!started && !processFinishedHandled)
    {
        processFinishedHandled = true;
        publishProcessTerminalResult(
            DesktopRunOutcome::FailedToStart,
            -1,
            false,
            processController.lastError());
    }
}

void DesktopRunSessionCoordinator::handleProcessStateChanged(
    DesktopRunState processState)
{
    switch (processState)
    {
    case DesktopRunState::Starting:
        setState(DesktopRunSessionCoordinatorState::Starting);
        break;
    case DesktopRunState::Running:
        setState(DesktopRunSessionCoordinatorState::Running);
        break;
    case DesktopRunState::Stopping:
        setState(DesktopRunSessionCoordinatorState::Stopping);
        break;
    case DesktopRunState::Idle:
    case DesktopRunState::Finished:
        break;
    }
}

void DesktopRunSessionCoordinator::handleProcessFinished(
    DesktopRunOutcome outcome,
    int exitCode,
    bool forcedKill)
{
    if (processFinishedHandled || !currentWorkspace)
        return;
    processFinishedHandled = true;
    publishProcessTerminalResult(
        outcome,
        exitCode,
        forcedKill,
        processController.lastError());
}

void DesktopRunSessionCoordinator::publishProcessTerminalResult(
    DesktopRunOutcome processOutcome,
    int exitCode,
    bool forcedKill,
    const QString& startFailureMessage)
{
    if (!currentWorkspace)
        return;

    currentWorkspaceResult.workspace = currentWorkspace;
    const DesktopRunSessionCoordinatorOutcome outcome =
        processOutcomeToCoordinatorOutcome(processOutcome);
    const QString message =
        !startFailureMessage.isEmpty() &&
            processOutcome == DesktopRunOutcome::FailedToStart
        ? startFailureMessage
        : fallbackProcessMessage(processOutcome, exitCode);
    publishTerminalResult(
        outcome,
        processOutcome,
        exitCode,
        forcedKill,
        message);
}

void DesktopRunSessionCoordinator::publishTerminalResult(
    DesktopRunSessionCoordinatorOutcome outcome,
    DesktopRunOutcome processOutcome,
    int exitCode,
    bool forcedKill,
    const QString& message)
{
    currentTerminalResult = {};
    currentTerminalResult.outcome = outcome;
    currentTerminalResult.processOutcome = processOutcome;
    currentTerminalResult.exitCode = exitCode;
    currentTerminalResult.forcedKill = forcedKill;
    currentTerminalResult.message = message;
    currentTerminalResult.workspaceResult = currentWorkspaceResult;
    if (currentWorkspace)
    {
        currentTerminalResult.sessionId = currentWorkspace->sessionId;
        currentTerminalResult.sessionPath =
            currentWorkspace->paths.sessionRoot;
    }
    const DesktopRunSessionTerminalResult published =
        currentTerminalResult;
    setState(DesktopRunSessionCoordinatorState::Terminal);
    emit terminalResultChanged(published);
}

bool DesktopRunSessionCoordinator::cleanupWorkspace(
    const DesktopRunSessionWorkspace& workspace,
    const QString& trustedSessionsBaseDirectory,
    QString* failureMessage)
{
    DesktopRunSessionCleanupRequest request;
    request.sessionId = workspace.sessionId;
    request.processActive = processController.isActive();
    const DesktopRunSessionCleanupResult result =
        cleanupDesktopRunSession(
            trustedSessionsBaseDirectory,
            workspace,
            request);
    if (!result.succeeded() && failureMessage)
        *failureMessage = result.message;
    return result.succeeded();
}

bool DesktopRunSessionCoordinator::cleanupCurrentSession(
    QString* failureMessage)
{
    if (!currentWorkspace)
        return true;
    if (processController.isActive())
    {
        if (failureMessage)
        {
            *failureMessage = QStringLiteral(
                "The current game process is still active");
        }
        return false;
    }
    if (!cleanupWorkspace(
            *currentWorkspace,
            currentWorkspaceResult.trustedSessionsBaseDirectory,
            failureMessage))
    {
        return false;
    }
    currentWorkspace.reset();
    currentPresentation.reset();
    currentWorkspaceResult = {};
    currentTerminalResult = {};
    return true;
}

void DesktopRunSessionCoordinator::
releaseBackgroundThreadForDestruction()
{
    QThread* worker = backgroundThread;
    if (worker == nullptr)
        return;
    backgroundThread = nullptr;
    disconnect(worker, nullptr, this, nullptr);

    const auto result = backgroundWorkspaceResult;
    const QString sessionsBase =
        currentRequest.trustedSessionsBaseDirectory;
    backgroundWorkspaceResult.reset();
    const auto cleanupPreparedWorkspace =
        [result, sessionsBase]()
        {
            if (!result || !result->cleanupWorkspace)
                return;
            DesktopRunSessionCleanupRequest request;
            request.sessionId =
                result->cleanupWorkspace->sessionId;
            request.processActive = false;
            cleanupDesktopRunSession(
                sessionsBase,
                *result->cleanupWorkspace,
                request);
        };
    connect(
        worker,
        &QThread::finished,
        worker,
        cleanupPreparedWorkspace,
        Qt::DirectConnection);
    if (worker->isFinished())
    {
        cleanupPreparedWorkspace();
        delete worker;
        return;
    }
    connect(
        worker,
        &QThread::finished,
        worker,
        &QObject::deleteLater,
        Qt::QueuedConnection);
}
