#include "StoryGraphAnalysisCoordinator.h"

#include "EditorProcessLifecycle.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThread>

#include <atomic>
#include <exception>
#include <utility>

namespace
{
constexpr char kCancelledLayoutCode[] =
    "story_graph.layout.cancelled";
constexpr char kUnavailableProjectLayoutCode[] =
    "story_graph.layout.project_unavailable";
constexpr char kPartialProjectLayoutCode[] =
    "story_graph.layout.partial_project";

StoryGraphProjectResult failedResult(
    const StoryGraphProjectRequest& request,
    const QString& message)
{
    StoryGraphProjectResult result;
    result.analysisGeneration =
        request.analysisGeneration;
    result.status = StoryGraphProjectStatus::Failed;
    result.controlFlowGraph.complete = false;
    result.semanticGraph.complete = false;

    StoryGraphWarning warning;
    warning.category =
        StoryGraphWarningCategory::Resolution;
    warning.severity =
        StoryGraphWarningSeverity::Error;
    warning.diagnosticCode =
        QStringLiteral(
            "story_graph.analysis.background_failed");
    warning.message = message;
    warning.source = request.entrySource.identity;
    result.warnings.append(std::move(warning));
    return result;
}

StoryGraphLayoutResult layoutStatusResult(
    StoryGraphLayoutStatus status,
    const QString& diagnosticCode,
    const QString& message)
{
    StoryGraphLayoutResult result;
    result.status = status;

    StoryGraphLayoutWarning warning;
    warning.diagnosticCode = diagnosticCode;
    warning.message = message;
    result.warnings.append(std::move(warning));
    return result;
}

void markLayoutsCancelled(
    StoryGraphAnalysisBundle* bundle)
{
    if (bundle == nullptr)
        return;

    const StoryGraphLayoutResult cancelled =
        layoutStatusResult(
            StoryGraphLayoutStatus::Cancelled,
            QString::fromLatin1(
                kCancelledLayoutCode),
            QCoreApplication::translate(
                "StoryGraphAnalysisCoordinator",
                "剧情图分析布局已取消。"));
    bundle->controlFlowLayout = cancelled;
    bundle->semanticLayout = cancelled;
}

void markLayoutsUnavailable(
    StoryGraphAnalysisBundle* bundle)
{
    if (bundle == nullptr)
        return;

    const StoryGraphLayoutResult unavailable =
        layoutStatusResult(
            StoryGraphLayoutStatus::Partial,
            QString::fromLatin1(
                kUnavailableProjectLayoutCode),
            QCoreApplication::translate(
                "StoryGraphAnalysisCoordinator",
                "没有可用于布局的项目图。"));
    bundle->controlFlowLayout = unavailable;
    bundle->semanticLayout = unavailable;
}

void markLayoutFromPartialProject(
    StoryGraphLayoutResult* layout)
{
    if (layout == nullptr ||
        layout->status !=
            StoryGraphLayoutStatus::Complete)
    {
        return;
    }

    layout->status =
        StoryGraphLayoutStatus::Partial;
    StoryGraphLayoutWarning warning;
    warning.diagnosticCode =
        QString::fromLatin1(
            kPartialProjectLayoutCode);
    warning.message =
        QCoreApplication::translate(
            "StoryGraphAnalysisCoordinator",
            "项目分析仅部分完成；布局只包含当前可用的图。");
    layout->warnings.append(std::move(warning));
}

void populateLayouts(
    StoryGraphAnalysisBundle* bundle,
    const StoryGraphLayout::CancelCallback&
        cancelCallback)
{
    if (bundle == nullptr)
        return;

    if ((cancelCallback && cancelCallback()) ||
        bundle->projectResult.wasCancelled())
    {
        markLayoutsCancelled(bundle);
        return;
    }
    if (!bundle->projectResult.hasUsableGraph())
    {
        markLayoutsUnavailable(bundle);
        return;
    }

    bundle->controlFlowLayout =
        StoryGraphLayout::layout(
            bundle->projectResult.controlFlowGraph,
            StoryGraphLayoutOptions(),
            cancelCallback);
    if (bundle->controlFlowLayout.wasCancelled() ||
        (cancelCallback && cancelCallback()))
    {
        markLayoutsCancelled(bundle);
        return;
    }

    bundle->semanticLayout =
        StoryGraphLayout::layout(
            bundle->projectResult.semanticGraph,
            StoryGraphLayoutOptions(),
            cancelCallback);
    if (bundle->semanticLayout.wasCancelled() ||
        (cancelCallback && cancelCallback()))
    {
        markLayoutsCancelled(bundle);
        return;
    }

    if (bundle->projectResult.status ==
        StoryGraphProjectStatus::Partial)
    {
        markLayoutFromPartialProject(
            &bundle->controlFlowLayout);
        markLayoutFromPartialProject(
            &bundle->semanticLayout);
    }
}
}

struct StoryGraphAnalysisCoordinator::WorkItem
{
    StoryGraphProjectRequest request;
    StoryGraphProjectResolver::ReadCallback
        readCallback;
    StoryGraphProjectResolver::
        MapFolderResolverCallback
            mapFolderResolver;
    std::shared_ptr<std::atomic_bool>
        cancellationRequested =
            std::make_shared<std::atomic_bool>(
                false);
    StoryGraphAnalysisBundle bundle;
    quint64 submissionSerial = 0;
};

bool StoryGraphAnalysisBundle::wasCancelled() const
{
    return projectResult.wasCancelled() ||
        controlFlowLayout.wasCancelled() ||
        semanticLayout.wasCancelled();
}

bool StoryGraphAnalysisBundle::hasUsableLayouts() const
{
    return projectResult.hasUsableGraph() &&
        controlFlowLayout.isUsable() &&
        semanticLayout.isUsable();
}

StoryGraphAnalysisCoordinator::
StoryGraphAnalysisCoordinator(QObject* parent)
    : QObject(parent)
{
}

StoryGraphAnalysisCoordinator::
~StoryGraphAnalysisCoordinator()
{
    shutdownRequested = true;
    detachActiveWorker(false);
}

bool StoryGraphAnalysisCoordinator::submit(
    const StoryGraphProjectRequest& request,
    StoryGraphProjectResolver::ReadCallback
        readCallback,
    StoryGraphProjectResolver::
        MapFolderResolverCallback
            mapFolderResolver)
{
    Q_ASSERT_X(
        isOwnerThread(),
        "StoryGraphAnalysisCoordinator::submit",
        "submit must run on the coordinator owner thread");
    if (!isOwnerThread() ||
        shutdownRequested)
    {
        return false;
    }

    auto work = std::make_shared<WorkItem>();
    work->request = request;
    work->readCallback =
        std::move(readCallback);
    work->mapFolderResolver =
        std::move(mapFolderResolver);
    work->submissionSerial =
        ++latestSubmissionSerial;
    currentLatestSubmittedGeneration =
        request.analysisGeneration;

    if (activeWorker != nullptr)
    {
        if (activeWork)
        {
            activeWork->cancellationRequested->store(
                true,
                std::memory_order_release);
        }
        pendingWork = std::move(work);
        publishGenerationStateChanged(true);
        updateBusyState(true);
        return true;
    }

    pendingWork.reset();
    startWork(work);
    updateBusyState(true);
    return true;
}

bool StoryGraphAnalysisCoordinator::isBusy() const
{
    return currentBusy;
}

bool StoryGraphAnalysisCoordinator::isShutdown() const
{
    return shutdownRequested;
}

std::optional<quint64>
StoryGraphAnalysisCoordinator::activeGeneration() const
{
    if (!activeWork)
        return std::nullopt;
    return activeWork->request.analysisGeneration;
}

std::optional<quint64>
StoryGraphAnalysisCoordinator::pendingGeneration() const
{
    if (!pendingWork)
        return std::nullopt;
    return pendingWork->request.analysisGeneration;
}

std::optional<quint64>
StoryGraphAnalysisCoordinator::
latestSubmittedGeneration() const
{
    return currentLatestSubmittedGeneration;
}

bool StoryGraphAnalysisCoordinator::cancel()
{
    Q_ASSERT_X(
        isOwnerThread(),
        "StoryGraphAnalysisCoordinator::cancel",
        "cancel must run on the coordinator owner thread");
    if (!isOwnerThread())
        return false;
    return cancelInternal(true);
}

void StoryGraphAnalysisCoordinator::shutdown()
{
    Q_ASSERT_X(
        isOwnerThread(),
        "StoryGraphAnalysisCoordinator::shutdown",
        "shutdown must run on the coordinator owner thread");
    if (!isOwnerThread() || shutdownRequested)
        return;
    shutdownRequested = true;
    detachActiveWorker(true);
}

bool StoryGraphAnalysisCoordinator::isOwnerThread() const
{
    return QThread::currentThread() == thread();
}

void StoryGraphAnalysisCoordinator::startWork(
    const std::shared_ptr<WorkItem>& work)
{
    Q_ASSERT(activeWorker == nullptr);
    Q_ASSERT(!activeWork);
    Q_ASSERT(work);

    activeWork = work;
    const std::shared_ptr<std::atomic_bool>
        cancellationRequested =
            work->cancellationRequested;
    QThread* worker = QThread::create(
        [work, cancellationRequested]()
        {
            const StoryGraphLayout::CancelCallback
                cancelCallback =
                    [cancellationRequested]()
                    {
                        return
                            cancellationRequested->
                                load(
                                    std::memory_order_acquire);
                    };
            try
            {
                work->bundle.projectResult =
                    StoryGraphProjectResolver::analyze(
                        work->request,
                        work->readCallback,
                        work->mapFolderResolver,
                        cancelCallback);
                populateLayouts(
                    &work->bundle,
                    cancelCallback);
            }
            catch (const std::exception& exception)
            {
                work->bundle = {};
                work->bundle.projectResult =
                    failedResult(
                        work->request,
                        QString::fromUtf8(
                            exception.what()));
                markLayoutsUnavailable(
                    &work->bundle);
            }
            catch (...)
            {
                work->bundle = {};
                work->bundle.projectResult =
                    failedResult(
                        work->request,
                        QCoreApplication::translate(
                            "StoryGraphAnalysisCoordinator",
                            "剧情图分析因未知异常而失败。"));
                markLayoutsUnavailable(
                    &work->bundle);
            }
        });
    registerEditorBackgroundWorker(worker);
    activeWorker = worker;
    const QPointer<QThread> workerGuard(worker);
    connect(
        worker,
        &QThread::finished,
        this,
        [this, workerGuard, work]()
        {
            QThread* finishedWorker =
                workerGuard.data();
            if (finishedWorker == nullptr)
                return;
            handleWorkerFinished(
                finishedWorker,
                work);
        });
    // Worker cleanup must not depend on the coordinator remaining alive. If
    // the owner event loop is already stopping, retaining this small object
    // is safer than destroying a running QThread.
    connect(
        worker,
        &QThread::finished,
        worker,
        &QObject::deleteLater);
    worker->start();
    publishGenerationStateChanged(true);
}

void StoryGraphAnalysisCoordinator::
handleWorkerFinished(
    QThread* worker,
    const std::shared_ptr<WorkItem>& work)
{
    if (worker != activeWorker ||
        work != activeWork)
    {
        return;
    }

    activeWorker = nullptr;
    activeWork.reset();

    const std::shared_ptr<WorkItem> nextWork =
        std::move(pendingWork);
    pendingWork.reset();
    if (nextWork && !shutdownRequested)
    {
        startWork(nextWork);
    }
    else
    {
        publishGenerationStateChanged(true);
    }
    updateBusyState(true);

    // State-change signals above may synchronously submit a newer request.
    // Re-check at the point of publication so such re-entrant refreshes also
    // invalidate this result.
    const bool resultIsCurrent =
        !shutdownRequested &&
        work->submissionSerial ==
            latestSubmissionSerial &&
        currentLatestSubmittedGeneration &&
        *currentLatestSubmittedGeneration ==
            work->request.analysisGeneration &&
        !work->cancellationRequested->load(
            std::memory_order_acquire) &&
        !work->bundle.wasCancelled();
    if (resultIsCurrent)
        emit analysisCompleted(work->bundle);
}

bool StoryGraphAnalysisCoordinator::cancelInternal(
    bool publishStateChanges)
{
    const bool hadWork =
        activeWorker != nullptr ||
        static_cast<bool>(pendingWork);
    if (!hadWork)
        return false;

    ++latestSubmissionSerial;
    currentLatestSubmittedGeneration.reset();
    if (activeWork)
    {
        activeWork->cancellationRequested->store(
            true,
            std::memory_order_release);
    }
    pendingWork.reset();
    publishGenerationStateChanged(
        publishStateChanges);
    updateBusyState(publishStateChanges);
    return true;
}

void StoryGraphAnalysisCoordinator::detachActiveWorker(
    bool publishStateChanges)
{
    cancelInternal(publishStateChanges);

    QThread* worker = activeWorker;
    if (worker != nullptr)
    {
        disconnect(worker, nullptr, this, nullptr);
        activeWorker = nullptr;
        activeWork.reset();
    }
    pendingWork.reset();
    publishGenerationStateChanged(
        publishStateChanges);
    updateBusyState(publishStateChanges);
}

void StoryGraphAnalysisCoordinator::
publishGenerationStateChanged(
    bool publishStateChanges)
{
    if (publishStateChanges)
        emit generationStateChanged();
}

void StoryGraphAnalysisCoordinator::updateBusyState(
    bool publishStateChanges)
{
    const bool nextBusy =
        activeWorker != nullptr ||
        static_cast<bool>(pendingWork);
    if (nextBusy == currentBusy)
        return;
    currentBusy = nextBusy;
    if (publishStateChanges)
        emit busyChanged(currentBusy);
}
