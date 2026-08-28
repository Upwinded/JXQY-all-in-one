#include "../core/StoryGraphAnalysisCoordinator.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool waitUntil(
    const std::function<bool()>& predicate,
    int timeoutMilliseconds = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() &&
           timer.elapsed() < timeoutMilliseconds)
    {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents,
            20);
        QThread::yieldCurrentThread();
    }
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        20);
    return predicate();
}

StoryGraphProjectRequest requestFor(
    quint64 generation,
    const QString& virtualPath,
    const QByteArray& source)
{
    StoryGraphProjectRequest request;
    request.analysisGeneration = generation;
    request.entrySource.identity.portableRootKey =
        QStringLiteral("active:test:0");
    request.entrySource.identity.virtualPath =
        virtualPath;
    request.entrySource.utf8Bytes = source;
    request.entryMapContext.state =
        StoryGraphMapContextState::Known;
    request.entryMapContext.effectiveMapFolder =
        QStringLiteral("test_map");

    StoryGraphContentRoot activeRoot;
    activeRoot.kind =
        StoryGraphContentRootKind::Active;
    activeRoot.portableRootKey =
        QStringLiteral("active:test:0");
    request.orderedContentRoots.append(
        activeRoot);
    return request;
}

class ReadGate
{
public:
    StoryGraphReadResult read(
        const StoryGraphContentRoot&,
        const QString&)
    {
        {
            const std::lock_guard<std::mutex> lock(
                mutex);
            entered = true;
            ++entryCount;
            ++activeCallbackCount;
            int maximum =
                maximumActiveCallbackCount.load();
            while (maximum <
                       activeCallbackCount.load() &&
                   !maximumActiveCallbackCount.
                        compare_exchange_weak(
                            maximum,
                            activeCallbackCount.load()))
            {
            }
        }
        condition.notify_all();

        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(
                lock,
                [this]()
                {
                    return released;
                });
        }

        --activeCallbackCount;
        callbackExited.store(true);
        StoryGraphReadResult result;
        result.status = StoryGraphReadStatus::Missing;
        return result;
    }

    bool waitForEntry(
        int timeoutMilliseconds = 5000)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(
            lock,
            std::chrono::milliseconds(
                timeoutMilliseconds),
            [this]()
            {
                return entered;
            });
    }

    void release()
    {
        {
            const std::lock_guard<std::mutex> lock(
                mutex);
            released = true;
        }
        condition.notify_all();
    }

    int entries() const
    {
        return entryCount.load();
    }

    int maximumConcurrentCallbacks() const
    {
        return maximumActiveCallbackCount.load();
    }

    bool exited() const
    {
        return callbackExited.load();
    }

private:
    mutable std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool released = false;
    std::atomic<int> entryCount{0};
    std::atomic<int> activeCallbackCount{0};
    std::atomic<int> maximumActiveCallbackCount{0};
    std::atomic_bool callbackExited{false};
};

StoryGraphProjectResolver::ReadCallback
blockingCallback(const std::shared_ptr<ReadGate>& gate)
{
    return [gate](
               const StoryGraphContentRoot& root,
               const QString& path)
        {
            return gate->read(root, path);
        };
}

bool hasLayoutWarning(
    const StoryGraphLayoutResult& layout,
    const QString& diagnosticCode)
{
    for (const StoryGraphLayoutWarning& warning :
         layout.warnings)
    {
        if (warning.diagnosticCode ==
            diagnosticCode)
        {
            return true;
        }
    }
    return false;
}

bool testNewestPendingRequestWins()
{
    StoryGraphAnalysisCoordinator coordinator;
    QList<StoryGraphAnalysisBundle> completions;
    QObject::connect(
        &coordinator,
        &StoryGraphAnalysisCoordinator::
            analysisCompleted,
        &coordinator,
        [&completions](
            const StoryGraphAnalysisBundle& bundle)
        {
            completions.append(bundle);
        });

    const auto activeGate =
        std::make_shared<ReadGate>();
    std::atomic<int> replacedCallbackCount{0};
    bool passed = true;
    passed &= check(
        coordinator.submit(
            requestFor(
                1,
                QStringLiteral(
                    "script/map/test_map/a.lua"),
                QByteArray(
                    "runscript(\"child.lua\")\n")),
            blockingCallback(activeGate)),
        "active request is accepted");
    passed &= check(
        activeGate->waitForEntry(),
        "active request reaches its reader");

    passed &= check(
        coordinator.submit(
            requestFor(
                2,
                QStringLiteral(
                    "script/map/test_map/b.lua"),
                QByteArray("return\n")),
            [&replacedCallbackCount](
                const StoryGraphContentRoot&,
                const QString&)
            {
                ++replacedCallbackCount;
                return StoryGraphReadResult();
            }),
        "first pending request is accepted");
    passed &= check(
        coordinator.submit(
            requestFor(
                3,
                QStringLiteral(
                    "script/map/test_map/c.lua"),
                QByteArray(
                    "runscript(\"child.lua\")\n")),
            blockingCallback(activeGate)),
        "newest pending request is accepted");
    passed &= check(
        coordinator.activeGeneration() ==
            std::optional<quint64>(1) &&
        coordinator.pendingGeneration() ==
            std::optional<quint64>(3) &&
        coordinator.latestSubmittedGeneration() ==
            std::optional<quint64>(3),
        "active and newest pending generations are exposed");

    activeGate->release();
    passed &= check(
        waitUntil(
            [&coordinator, &completions]()
            {
                return !coordinator.isBusy() &&
                    completions.size() == 1;
            }),
        "newest pending request completes");
    passed &= check(
        completions.size() == 1 &&
        completions.constFirst().projectResult.
                analysisGeneration == 3 &&
        !completions.constFirst().projectResult.
                documents.
                isEmpty() &&
        completions.constFirst().
                hasUsableLayouts() &&
        !completions.constFirst().
                semanticLayout.nodeRectangles.
                isEmpty() &&
        completions.constFirst().projectResult.
                documents.
                constFirst().source.virtualPath ==
            QStringLiteral(
                "script/map/test_map/c.lua"),
        "only newest pending result is published");
    passed &= check(
        replacedCallbackCount.load() == 0,
        "replaced pending request never starts");
    passed &= check(
        activeGate->entries() >= 2 &&
        activeGate->maximumConcurrentCallbacks() == 1,
        "active and replacement callbacks never overlap");
    return passed;
}

bool testSubmissionIdentityDiscardsSameGeneration()
{
    StoryGraphAnalysisCoordinator coordinator;
    QList<StoryGraphAnalysisBundle> completions;
    QObject::connect(
        &coordinator,
        &StoryGraphAnalysisCoordinator::
            analysisCompleted,
        &coordinator,
        [&completions](
            const StoryGraphAnalysisBundle& bundle)
        {
            completions.append(bundle);
        });

    const auto activeGate =
        std::make_shared<ReadGate>();
    bool passed = true;
    passed &= check(
        coordinator.submit(
            requestFor(
                9,
                QStringLiteral(
                    "script/map/test_map/old.lua"),
                QByteArray(
                    "runscript(\"child.lua\")\n")),
            blockingCallback(activeGate)) &&
        activeGate->waitForEntry(),
        "same-generation old request starts");
    passed &= check(
        coordinator.submit(
            requestFor(
                9,
                QStringLiteral(
                    "script/map/test_map/new.lua"),
                QByteArray("return\n")),
            StoryGraphProjectResolver::ReadCallback()),
        "same-generation replacement is accepted");

    activeGate->release();
    passed &= check(
        waitUntil(
            [&coordinator, &completions]()
            {
                return !coordinator.isBusy() &&
                    completions.size() == 1;
            }),
        "same-generation replacement completes");
    passed &= check(
        !completions.constFirst().projectResult.
                documents.
                isEmpty() &&
        completions.constFirst().projectResult.
                documents.
                constFirst().source.virtualPath ==
            QStringLiteral(
                "script/map/test_map/new.lua"),
        "submission identity prevents a stale equal generation result");
    return passed;
}

bool testExplicitCancellationDropsPendingAndResult()
{
    StoryGraphAnalysisCoordinator coordinator;
    QList<StoryGraphAnalysisBundle> completions;
    QObject::connect(
        &coordinator,
        &StoryGraphAnalysisCoordinator::
            analysisCompleted,
        &coordinator,
        [&completions](
            const StoryGraphAnalysisBundle& bundle)
        {
            completions.append(bundle);
        });

    const auto activeGate =
        std::make_shared<ReadGate>();
    std::atomic<int> pendingCallbackCount{0};
    bool passed = true;
    passed &= check(
        coordinator.submit(
            requestFor(
                20,
                QStringLiteral(
                    "script/map/test_map/active.lua"),
                QByteArray(
                    "runscript(\"child.lua\")\n")),
            blockingCallback(activeGate)) &&
        activeGate->waitForEntry(),
        "cancellation fixture starts");
    passed &= check(
        coordinator.submit(
            requestFor(
                21,
                QStringLiteral(
                    "script/map/test_map/pending.lua"),
                QByteArray(
                    "runscript(\"child.lua\")\n")),
            [&pendingCallbackCount](
                const StoryGraphContentRoot&,
                const QString&)
            {
                ++pendingCallbackCount;
                return StoryGraphReadResult();
            }),
        "cancellation fixture queues pending request");
    QElapsedTimer cancelTimer;
    cancelTimer.start();
    const bool cancelReturned = coordinator.cancel();
    const qint64 cancelMilliseconds =
        cancelTimer.elapsed();
    passed &= check(
        cancelReturned &&
        cancelMilliseconds < 250 &&
        !coordinator.pendingGeneration() &&
        !coordinator.latestSubmittedGeneration(),
        "cancel promptly invalidates active and pending generations without joining the blocked worker");

    activeGate->release();
    passed &= check(
        waitUntil(
            [&coordinator]()
            {
                return !coordinator.isBusy();
            }),
        "cancelled worker exits");
    passed &= check(
        completions.isEmpty() &&
        pendingCallbackCount.load() == 0,
        "cancelled and pending results are never published");
    passed &= check(
        !coordinator.cancel(),
        "cancelling idle coordinator reports no work");
    return passed;
}

bool testDestructorAndShutdownDoNotWait()
{
    const auto destructorGate =
        std::make_shared<ReadGate>();
    auto coordinator =
        std::make_unique<
            StoryGraphAnalysisCoordinator>();
    bool passed = true;
    passed &= check(
        coordinator->submit(
            requestFor(
                40,
                QStringLiteral(
                    "script/map/test_map/destructor.lua"),
                QByteArray(
                    "runscript(\"child.lua\")\n")),
            blockingCallback(destructorGate)) &&
        destructorGate->waitForEntry(),
        "destructor fixture starts");

    std::atomic_bool destructorReturned{false};
    std::thread destructorFallbackRelease(
        [destructorGate, &destructorReturned]()
        {
            for (int attempt = 0;
                 attempt < 75 &&
                 !destructorReturned.load();
                 ++attempt)
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
            }
            if (!destructorReturned.load())
                destructorGate->release();
        });
    QElapsedTimer destructorTimer;
    destructorTimer.start();
    coordinator.reset();
    const qint64 destructorMilliseconds =
        destructorTimer.elapsed();
    destructorReturned.store(true);
    destructorGate->release();
    destructorFallbackRelease.join();
    passed &= check(
        destructorMilliseconds < 250,
        "destructor returns without joining the active worker");
    passed &= check(
        waitUntil(
            [destructorGate]()
            {
                return destructorGate->exited();
            }),
        "detached destructor worker exits safely after its gate is released");

    StoryGraphAnalysisCoordinator shutdownCoordinator;
    const auto shutdownGate =
        std::make_shared<ReadGate>();
    passed &= check(
        shutdownCoordinator.submit(
            requestFor(
                41,
                QStringLiteral(
                    "script/map/test_map/shutdown.lua"),
                QByteArray(
                    "runscript(\"child.lua\")\n")),
            blockingCallback(shutdownGate)) &&
        shutdownGate->waitForEntry(),
        "shutdown fixture starts");
    std::atomic_bool shutdownReturned{false};
    std::thread shutdownFallbackRelease(
        [shutdownGate, &shutdownReturned]()
        {
            for (int attempt = 0;
                 attempt < 75 &&
                 !shutdownReturned.load();
                 ++attempt)
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
            }
            if (!shutdownReturned.load())
                shutdownGate->release();
        });
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    shutdownCoordinator.shutdown();
    const qint64 shutdownMilliseconds =
        shutdownTimer.elapsed();
    shutdownReturned.store(true);
    shutdownGate->release();
    shutdownFallbackRelease.join();
    passed &= check(
        shutdownCoordinator.isShutdown() &&
        !shutdownCoordinator.isBusy() &&
        shutdownMilliseconds < 250 &&
        !shutdownCoordinator.submit(
            requestFor(
                42,
                QStringLiteral(
                    "script/map/test_map/rejected.lua"),
                QByteArray("return\n")),
            StoryGraphProjectResolver::ReadCallback()),
        "shutdown detaches promptly and permanently rejects later submissions");
    passed &= check(
        waitUntil(
            [shutdownGate]()
            {
                return shutdownGate->exited();
            }),
        "detached shutdown worker exits safely after its gate is released");
    shutdownCoordinator.shutdown();
    return passed;
}

bool testSuccessfulResult()
{
    StoryGraphAnalysisCoordinator coordinator;
    QList<StoryGraphAnalysisBundle> completions;
    QList<bool> busyTransitions;
    QObject::connect(
        &coordinator,
        &StoryGraphAnalysisCoordinator::
            analysisCompleted,
        &coordinator,
        [&completions](
            const StoryGraphAnalysisBundle& bundle)
        {
            completions.append(bundle);
        });
    QObject::connect(
        &coordinator,
        &StoryGraphAnalysisCoordinator::busyChanged,
        &coordinator,
        [&busyTransitions](bool busy)
        {
            busyTransitions.append(busy);
        });

    bool passed = true;
    passed &= check(
        coordinator.submit(
            requestFor(
                50,
                QStringLiteral(
                    "script/map/test_map/success.lua"),
                QByteArray(
                    "say(\"ready\")\n"
                    "return\n")),
            StoryGraphProjectResolver::ReadCallback()),
        "successful request is accepted");
    passed &= check(
        waitUntil(
            [&coordinator, &completions]()
            {
                return !coordinator.isBusy() &&
                    completions.size() == 1;
            }),
        "successful result is emitted");
    passed &= check(
        completions.constFirst().projectResult.
                status ==
            StoryGraphProjectStatus::Complete &&
        completions.constFirst().projectResult.
                analysisGeneration == 50 &&
        completions.constFirst().
                hasUsableLayouts() &&
        completions.constFirst().
                controlFlowLayout.status ==
            StoryGraphLayoutStatus::Complete &&
        completions.constFirst().
                semanticLayout.status ==
            StoryGraphLayoutStatus::Complete &&
        completions.constFirst().
                controlFlowLayout.nodeRectangles.
                size() ==
            completions.constFirst().
                projectResult.controlFlowGraph.nodes.
                size() &&
        completions.constFirst().
                semanticLayout.nodeRectangles.size() ==
            completions.constFirst().
                projectResult.semanticGraph.nodes.size() &&
        !completions.constFirst().
                semanticLayout.nodeRectangles.
                isEmpty(),
        "completed signal contains both finished worker layouts");
    passed &= check(
        busyTransitions ==
            QList<bool>({true, false}),
        "busy state exposes one complete lifecycle");
    return passed;
}

bool testPartialAndUnavailableLayoutStates()
{
    StoryGraphAnalysisCoordinator coordinator;
    QList<StoryGraphAnalysisBundle> completions;
    QObject::connect(
        &coordinator,
        &StoryGraphAnalysisCoordinator::
            analysisCompleted,
        &coordinator,
        [&completions](
            const StoryGraphAnalysisBundle& bundle)
        {
            completions.append(bundle);
        });

    bool passed = true;
    passed &= check(
        coordinator.submit(
            requestFor(
                55,
                QStringLiteral(
                    "script/map/test_map/partial.lua"),
                QByteArray(
                    "if true then\n"
                    "  say(\"available\")\n")),
            StoryGraphProjectResolver::ReadCallback()),
        "partial analysis request is accepted");
    passed &= check(
        waitUntil(
            [&coordinator, &completions]()
            {
                return !coordinator.isBusy() &&
                    completions.size() == 1;
            }),
        "partial analysis bundle completes");
    passed &= check(
        completions.constFirst().projectResult.
                status ==
            StoryGraphProjectStatus::Partial &&
        completions.constFirst().
                controlFlowLayout.status ==
            StoryGraphLayoutStatus::Partial &&
        completions.constFirst().
                semanticLayout.status ==
            StoryGraphLayoutStatus::Partial &&
        completions.constFirst().
                hasUsableLayouts() &&
        !completions.constFirst().
                controlFlowLayout.nodeRectangles.
                isEmpty() &&
        !completions.constFirst().
                semanticLayout.nodeRectangles.
                isEmpty(),
        "partial resolver output produces explicit usable partial layouts");

    completions.clear();
    StoryGraphProjectRequest failedRequest =
        requestFor(
            56,
            QStringLiteral(
                "script/map/test_map/failed.lua"),
            QByteArray("return\n"));
    failedRequest.budget.maximumFileCount = 0;
    passed &= check(
        coordinator.submit(
            failedRequest,
            StoryGraphProjectResolver::ReadCallback()),
        "failed analysis request is accepted");
    passed &= check(
        waitUntil(
            [&coordinator, &completions]()
            {
                return !coordinator.isBusy() &&
                    completions.size() == 1;
            }),
        "failed analysis bundle completes");
    passed &= check(
        completions.constFirst().projectResult.
                status ==
            StoryGraphProjectStatus::Failed &&
        completions.constFirst().
                controlFlowLayout.status ==
            StoryGraphLayoutStatus::Partial &&
        completions.constFirst().
                semanticLayout.status ==
            StoryGraphLayoutStatus::Partial &&
        !completions.constFirst().
                hasUsableLayouts() &&
        completions.constFirst().
                controlFlowLayout.nodeRectangles.
                isEmpty() &&
        completions.constFirst().
                semanticLayout.nodeRectangles.
                isEmpty() &&
        hasLayoutWarning(
            completions.constFirst().
                controlFlowLayout,
            QStringLiteral(
                "story_graph.layout.project_unavailable")) &&
        hasLayoutWarning(
            completions.constFirst().
                semanticLayout,
            QStringLiteral(
                "story_graph.layout.project_unavailable")),
        "failed project exposes explicit partial unavailable layouts");
    return passed;
}

bool testReentrantRefreshDiscardsFinishedResult()
{
    StoryGraphAnalysisCoordinator coordinator;
    QList<StoryGraphAnalysisBundle> completions;
    bool replacementSubmitted = false;
    QObject::connect(
        &coordinator,
        &StoryGraphAnalysisCoordinator::
            analysisCompleted,
        &coordinator,
        [&completions](
            const StoryGraphAnalysisBundle& bundle)
        {
            completions.append(bundle);
        });
    QObject::connect(
        &coordinator,
        &StoryGraphAnalysisCoordinator::busyChanged,
        &coordinator,
        [&coordinator, &replacementSubmitted](
            bool busy)
        {
            if (busy || replacementSubmitted)
                return;
            replacementSubmitted = true;
            coordinator.submit(
                requestFor(
                    61,
                    QStringLiteral(
                        "script/map/test_map/reentrant-new.lua"),
                    QByteArray("return\n")),
                StoryGraphProjectResolver::
                    ReadCallback());
        });

    bool passed = true;
    passed &= check(
        coordinator.submit(
            requestFor(
                60,
                QStringLiteral(
                    "script/map/test_map/reentrant-old.lua"),
                QByteArray("return\n")),
            StoryGraphProjectResolver::ReadCallback()),
        "reentrant refresh fixture is accepted");
    passed &= check(
        waitUntil(
            [&coordinator, &completions]()
            {
                return !coordinator.isBusy() &&
                    completions.size() == 1;
            }),
        "reentrant replacement completes");
    passed &= check(
        replacementSubmitted &&
        completions.size() == 1 &&
        completions.constFirst().projectResult.
                analysisGeneration == 61,
        "state-change refresh invalidates the just-finished result");
    return passed;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;
    passed &= testNewestPendingRequestWins();
    passed &=
        testSubmissionIdentityDiscardsSameGeneration();
    passed &=
        testExplicitCancellationDropsPendingAndResult();
    passed &= testDestructorAndShutdownDoNotWait();
    passed &= testSuccessfulResult();
    passed &= testPartialAndUnavailableLayoutStates();
    passed &=
        testReentrantRefreshDiscardsFinishedResult();
    if (passed)
    {
        std::cout
            << "All story graph analysis coordinator tests passed\n";
    }
    return passed ? 0 : 1;
}
