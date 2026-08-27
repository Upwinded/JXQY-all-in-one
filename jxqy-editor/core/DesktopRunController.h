#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringConverter>
#include <QTimer>

#include <cstdint>
#include <memory>

struct DesktopRunSessionWorkspace;

enum class DesktopRunState
{
    Idle,
    Starting,
    Running,
    Stopping,
    Finished
};

enum class DesktopRunOutcome
{
    None,
    Succeeded,
    NonZeroExit,
    Crashed,
    FailedToStart,
    StoppedByUser
};

class DesktopRunController : public QObject
{
    Q_OBJECT

public:
    explicit DesktopRunController(QObject* parent = nullptr);
    ~DesktopRunController() override;

    DesktopRunState state() const;
    DesktopRunOutcome outcome() const;
    int exitCode() const;
    bool forcedKill() const;
    QString lastError() const;
    QString executablePath() const;
    QString descriptorPath() const;
    bool isActive() const;

    bool start(
        const QString& executablePath,
        const QString& descriptorPath,
        const QString& workingDirectory);
    bool start(
        const QString& executablePath,
        const DesktopRunSessionWorkspace& workspace,
        const QString& workingDirectory);
    void requestStop();
    // Force-stops an active child process and waits for at most three seconds
    // so the caller can remove the current private run directory on exit.
    bool stopImmediatelyForApplicationExit();
    void setStopTimeoutMilliseconds(int milliseconds);

signals:
    void stateChanged(DesktopRunState state);
    void standardOutputReceived(const QString& text);
    void standardErrorReceived(const QString& text);
    void runFinished(
        DesktopRunOutcome outcome,
        int exitCode,
        bool forcedKill);

private:
    struct PinnedLaunchTargets;

    bool startInternal(
        const QString& executablePath,
        const QString& descriptorPath,
        const QString& workingDirectory,
        const QByteArray& expectedDescriptorSha256);
    void setState(DesktopRunState state);
    void drainStandardOutput();
    void drainStandardError();
    void finishDecoder(
        QStringDecoder& decoder,
        bool standardError);
    void finishRun(
        DesktopRunOutcome outcome,
        int exitCode);

    QProcess* process = nullptr;
    QTimer stopTimer;
    QStringDecoder standardOutputDecoder{QStringDecoder::Utf8};
    QStringDecoder standardErrorDecoder{QStringDecoder::Utf8};
    DesktopRunState currentState = DesktopRunState::Idle;
    DesktopRunOutcome currentOutcome = DesktopRunOutcome::None;
    int currentExitCode = 0;
    bool userStopRequested = false;
    bool processWasKilled = false;
    std::uint64_t currentRunGeneration = 0;
    QString currentError;
    QString currentExecutablePath;
    QString currentDescriptorPath;
    std::unique_ptr<PinnedLaunchTargets> pinnedLaunchTargets;
};
