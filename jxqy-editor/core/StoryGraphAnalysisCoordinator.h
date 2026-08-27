#pragma once

#include "StoryGraphLayout.h"
#include "StoryGraphProjectResolver.h"

#include <QObject>

#include <memory>
#include <optional>

class QThread;

struct StoryGraphAnalysisBundle
{
    StoryGraphProjectResult projectResult;
    StoryGraphLayoutResult controlFlowLayout;
    StoryGraphLayoutResult semanticLayout;

    bool wasCancelled() const;
    bool hasUsableLayouts() const;
};

// Coordinates project analysis without exposing QThread ownership to the UI.
// All public methods must be called from the QObject's owner thread. Resolver
// callbacks run on the worker thread and therefore must not access QWidget or
// other thread-bound QObject state.
class StoryGraphAnalysisCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit StoryGraphAnalysisCoordinator(
        QObject* parent = nullptr);
    ~StoryGraphAnalysisCoordinator() override;

    bool submit(
        const StoryGraphProjectRequest& request,
        StoryGraphProjectResolver::ReadCallback
            readCallback,
        StoryGraphProjectResolver::
            MapFolderResolverCallback
                mapFolderResolver =
                    StoryGraphProjectResolver::
                        MapFolderResolverCallback());

    bool isBusy() const;
    bool isShutdown() const;
    std::optional<quint64> activeGeneration() const;
    std::optional<quint64> pendingGeneration() const;
    std::optional<quint64>
    latestSubmittedGeneration() const;

    // Cancels the active request and drops the pending request. Cancellation
    // is cooperative; isBusy() stays true until the active worker exits.
    bool cancel();

    // Permanently rejects future submissions and cooperatively cancels active
    // work without waiting for the worker thread. Calling shutdown() more than
    // once is safe.
    void shutdown();

signals:
    void busyChanged(bool busy);
    void generationStateChanged();
    void analysisCompleted(
        const StoryGraphAnalysisBundle& bundle);

private:
    struct WorkItem;

    bool isOwnerThread() const;
    void startWork(
        const std::shared_ptr<WorkItem>& work);
    void handleWorkerFinished(
        QThread* worker,
        const std::shared_ptr<WorkItem>& work);
    bool cancelInternal(bool publishStateChanges);
    void detachActiveWorker(
        bool publishStateChanges);
    void publishGenerationStateChanged(
        bool publishStateChanges);
    void updateBusyState(bool publishStateChanges);

    QThread* activeWorker = nullptr;
    std::shared_ptr<WorkItem> activeWork;
    std::shared_ptr<WorkItem> pendingWork;
    std::optional<quint64>
        currentLatestSubmittedGeneration;
    quint64 latestSubmissionSerial = 0;
    bool currentBusy = false;
    bool shutdownRequested = false;
};
