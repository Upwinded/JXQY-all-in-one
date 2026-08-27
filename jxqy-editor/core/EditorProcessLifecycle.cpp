#include "EditorProcessLifecycle.h"

#include <QThread>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace
{
struct ExitProtectedWriteWorkerState
{
    std::mutex mutex;
    std::condition_variable finishedCondition;
    bool finished = false;
    std::function<void()> requestCancellation;
};

std::atomic<quint64>& activeWorkerCount()
{
    // Deliberately process-lifetime storage. A detached worker may finish
    // while ordinary static objects are being considered for destruction.
    static auto* count =
        new std::atomic<quint64>(0);
    return *count;
}

std::mutex& exitProtectedWriteWorkersMutex()
{
    static auto* mutex = new std::mutex;
    return *mutex;
}

std::vector<std::weak_ptr<
    ExitProtectedWriteWorkerState>>&
exitProtectedWriteWorkers()
{
    static auto* workers =
        new std::vector<std::weak_ptr<
            ExitProtectedWriteWorkerState>>;
    return *workers;
}

std::vector<std::shared_ptr<
    ExitProtectedWriteWorkerState>>
activeExitProtectedWriteWorkers()
{
    std::vector<std::shared_ptr<
        ExitProtectedWriteWorkerState>>
        activeWorkers;
    const std::lock_guard<std::mutex> lock(
        exitProtectedWriteWorkersMutex());
    auto& registeredWorkers =
        exitProtectedWriteWorkers();
    auto worker = registeredWorkers.begin();
    while (worker != registeredWorkers.end())
    {
        std::shared_ptr<
            ExitProtectedWriteWorkerState>
            state = worker->lock();
        if (!state)
        {
            worker = registeredWorkers.erase(
                worker);
            continue;
        }
        activeWorkers.push_back(
            std::move(state));
        ++worker;
    }
    return activeWorkers;
}
}

void registerEditorBackgroundWorker(QThread* worker)
{
    if (worker == nullptr)
        return;

    activeWorkerCount().fetch_add(
        1,
        std::memory_order_acq_rel);
    QObject::connect(
        worker,
        &QObject::destroyed,
        worker,
        [](QObject*)
        {
            activeWorkerCount().fetch_sub(
                1,
                std::memory_order_acq_rel);
        },
        Qt::DirectConnection);
}

void registerEditorExitProtectedWriteWorker(
    QThread* worker,
    std::function<void()> requestCancellation)
{
    if (worker == nullptr)
        return;

    auto state = std::make_shared<
        ExitProtectedWriteWorkerState>();
    state->requestCancellation =
        std::move(requestCancellation);
    {
        const std::lock_guard<std::mutex> lock(
            exitProtectedWriteWorkersMutex());
        exitProtectedWriteWorkers().push_back(
            state);
    }
    QObject::connect(
        worker,
        &QThread::finished,
        worker,
        [state]()
        {
            {
                const std::lock_guard<std::mutex>
                    lock(state->mutex);
                state->finished = true;
            }
            state->finishedCondition.notify_all();
        },
        Qt::DirectConnection);
}

quint64 activeEditorBackgroundWorkerCount()
{
    return activeWorkerCount().load(
        std::memory_order_acquire);
}

int finishEditorApplicationExit(int exitCode)
{
    const auto writeWorkers =
        activeExitProtectedWriteWorkers();
    for (const auto& state : writeWorkers)
    {
        std::function<void()> requestCancellation;
        {
            const std::lock_guard<std::mutex> lock(
                state->mutex);
            if (!state->finished)
            {
                requestCancellation =
                    state->requestCancellation;
            }
        }
        if (requestCancellation)
            requestCancellation();
    }
    for (const auto& state : writeWorkers)
    {
        std::unique_lock<std::mutex> lock(
            state->mutex);
        state->finishedCondition.wait(
            lock,
            [state]()
            {
                return state->finished;
            });
    }

    if (activeEditorBackgroundWorkerCount() != 0)
        std::_Exit(exitCode);
    return exitCode;
}
