#include "../Game/Loading/ExclusiveLoadingRunner.h"

#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
constexpr std::chrono::seconds TestTimeout(2);

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool waitUntilFinalized(
	GameLoading::ExclusiveLoadingRunner& runner,
	const GameLoading::ExclusiveLoadingRunner::PumpCallback& pump,
	const GameLoading::ExclusiveLoadingRunner::FinalizeCallback&
		finalize)
{
	const auto deadline =
		std::chrono::steady_clock::now() + TestTimeout;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (runner.poll(pump, finalize) ==
			GameLoading::ExclusiveLoadingPollStatus::Finalized)
		{
			return true;
		}
		std::this_thread::yield();
	}
	return false;
}

bool runSuccessAndFinalizeOnceTest()
{
	std::promise<void> workerEnteredPromise;
	std::future<void> workerEntered =
		workerEnteredPromise.get_future();
	std::promise<void> releaseWorkerPromise;
	std::shared_future<void> releaseWorker =
		releaseWorkerPromise.get_future().share();

	GameLoading::ExclusiveLoadingRunner runner(
		[&](
			const GameLoading::LoadingCancellationToken&)
		{
			workerEnteredPromise.set_value();
			releaseWorker.wait();
			return GameLoading::LoadingTaskResult::success();
		});
	if (!check(
		workerEntered.wait_for(TestTimeout) ==
			std::future_status::ready,
		"success worker starts"))
	{
		releaseWorkerPromise.set_value();
		return false;
	}

	int pumpCount = 0;
	int finalizeCount = 0;
	bool workerReleased = false;
	GameLoading::LoadingTaskStatus finalizedStatus =
		GameLoading::LoadingTaskStatus::Failed;
	const auto pump =
		[&](GameLoading::ExclusiveLoadingRunner&)
		{
			++pumpCount;
			if (!workerReleased)
			{
				workerReleased = true;
				releaseWorkerPromise.set_value();
			}
		};
	const auto finalize =
		[&](const GameLoading::ExclusiveLoadingCompletion&
			completion)
		{
			++finalizeCount;
			finalizedStatus = completion.taskResult.status;
		};

	bool ok = check(
		waitUntilFinalized(runner, pump, finalize),
		"success worker finalizes before timeout");
	ok = check(
		pumpCount > 0,
		"owner pump runs while success worker is active") && ok;
	ok = check(
		finalizeCount == 1,
		"success finalize callback runs once") && ok;
	ok = check(
		finalizedStatus ==
			GameLoading::LoadingTaskStatus::Succeeded,
		"success result is published") && ok;
	ok = check(
		runner.completion() != nullptr &&
			runner.completion()->taskResult.succeeded(),
		"success completion remains available") && ok;

	const int pumpCountAfterFinalization = pumpCount;
	runner.poll(pump, finalize);
	runner.poll(pump, finalize);
	ok = check(
		pumpCount == pumpCountAfterFinalization,
		"pump does not run after finalization") && ok;
	ok = check(
		finalizeCount == 1,
		"finalize is not repeated by later polls") && ok;
	return ok;
}

bool runFailureResultTest()
{
	GameLoading::ExclusiveLoadingRunner runner(
		[](
			const GameLoading::LoadingCancellationToken&)
		{
			return GameLoading::LoadingTaskResult::failure(
				"fixture read failed");
		});
	int finalizeCount = 0;
	GameLoading::LoadingTaskResult finalizedResult;
	const bool finalized = waitUntilFinalized(
		runner,
		{},
		[&](const GameLoading::ExclusiveLoadingCompletion&
			completion)
		{
			++finalizeCount;
			finalizedResult = completion.taskResult;
		});
	bool ok = check(
		finalized,
		"failed worker finalizes before timeout");
	ok = check(
		finalizeCount == 1,
		"failed worker finalizes once") && ok;
	ok = check(
		finalizedResult.status ==
			GameLoading::LoadingTaskStatus::Failed &&
			finalizedResult.message == "fixture read failed" &&
			!finalizedResult.exception,
		"typed worker failure is preserved") && ok;
	return ok;
}

bool runOwnerFinalizationReadinessTest()
{
	GameLoading::ExclusiveLoadingRunner runner(
		[](
			const GameLoading::LoadingCancellationToken&)
		{
			return GameLoading::LoadingTaskResult::success();
		});
	const auto deadline =
		std::chrono::steady_clock::now() + TestTimeout;
	while (!runner.isWorkerComplete() &&
		std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::yield();
	}
	if (!check(
			runner.isWorkerComplete(),
			"readiness worker completes before timeout"))
	{
		return false;
	}

	int pumpCount = 0;
	int finalizeCount = 0;
	bool ownerFinalizationReady = true;
	const auto pump =
		[&](GameLoading::ExclusiveLoadingRunner&)
		{
			++pumpCount;
			if (pumpCount == 1)
			{
				// Simulate a background lifecycle event consumed by this
				// pump. A pre-pump readiness snapshot would incorrectly
				// finalize this already-completed worker.
				ownerFinalizationReady = false;
			}
		};
	const auto finalize =
		[&](const GameLoading::ExclusiveLoadingCompletion&)
		{
			++finalizeCount;
		};
	bool ok = check(
		runner.poll(
			pump,
			finalize,
			[&ownerFinalizationReady]()
			{
				return ownerFinalizationReady;
			}) ==
			GameLoading::ExclusiveLoadingPollStatus::Running &&
			!runner.isFinalized() &&
			runner.completion() == nullptr &&
			finalizeCount == 0,
		"readiness is re-evaluated after the pump before a completed worker is finalized");
	ok = check(
		runner.poll(
			pump,
			finalize,
			[&ownerFinalizationReady]()
			{
				return ownerFinalizationReady;
			}) ==
			GameLoading::ExclusiveLoadingPollStatus::Running &&
			pumpCount == 2 &&
			finalizeCount == 0,
		"owner pumping continues while completed work waits for readiness") &&
		ok;
	ownerFinalizationReady = true;
	ok = check(
		runner.poll(
			pump,
			finalize,
			[&ownerFinalizationReady]()
			{
				return ownerFinalizationReady;
			}) ==
			GameLoading::ExclusiveLoadingPollStatus::Finalized &&
			runner.isFinalized() &&
			runner.completion() != nullptr &&
			runner.completion()->taskResult.succeeded() &&
			finalizeCount == 1,
		"restoring owner readiness collects and finalizes exactly once") &&
		ok;
	(void)runner.poll(
		pump,
		finalize,
		[&ownerFinalizationReady]()
		{
			return ownerFinalizationReady;
		});
	ok = check(
		finalizeCount == 1,
		"readiness restoration cannot repeat finalization") &&
		ok;
	return ok;
}

bool runWorkerExceptionTest()
{
	GameLoading::ExclusiveLoadingRunner runner(
		[](
			const GameLoading::LoadingCancellationToken&)
			-> GameLoading::LoadingTaskResult
		{
			throw std::runtime_error("fixture worker exception");
		});
	bool finalized = waitUntilFinalized(runner, {}, {});
	const GameLoading::ExclusiveLoadingCompletion* completion =
		runner.completion();
	bool ok = check(
		finalized,
		"throwing worker finalizes before timeout");
	ok = check(
		completion != nullptr &&
			completion->taskResult.status ==
				GameLoading::LoadingTaskStatus::Failed &&
			completion->taskResult.message ==
				"fixture worker exception" &&
			static_cast<bool>(
				completion->taskResult.exception),
		"worker exception is captured as a failed result") && ok;

	bool rethrewOriginalException = false;
	if (completion && completion->taskResult.exception)
	{
		try
		{
			std::rethrow_exception(
				completion->taskResult.exception);
		}
		catch (const std::runtime_error& error)
		{
			rethrewOriginalException =
				std::string(error.what()) ==
					"fixture worker exception";
		}
		catch (...)
		{
		}
	}
	ok = check(
		rethrewOriginalException,
		"captured worker exception retains its type and message") && ok;
	return ok;
}

bool runCooperativeCancellationTest()
{
	std::mutex mutex;
	std::condition_variable condition;
	bool inspectCancellation = false;
	std::promise<void> workerEnteredPromise;
	std::future<void> workerEntered =
		workerEnteredPromise.get_future();

	GameLoading::ExclusiveLoadingRunner runner(
		[&](
			const GameLoading::LoadingCancellationToken&
				cancellationToken)
		{
			workerEnteredPromise.set_value();
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(
				lock,
				[&]()
				{
					return inspectCancellation;
				});
			return cancellationToken.isCancellationRequested()
				? GameLoading::LoadingTaskResult::cancellation(
					"fixture cancelled")
				: GameLoading::LoadingTaskResult::failure(
					"cancellation was not published");
		});
	if (!check(
		workerEntered.wait_for(TestTimeout) ==
			std::future_status::ready,
		"cancellation worker starts"))
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			inspectCancellation = true;
		}
		condition.notify_all();
		return false;
	}

	runner.requestCancellation();
	{
		std::lock_guard<std::mutex> lock(mutex);
		inspectCancellation = true;
	}
	condition.notify_all();

	bool ok = check(
		runner.isCancellationRequested(),
		"owner observes published cancellation request");
	ok = check(
		waitUntilFinalized(runner, {}, {}),
		"cancelled worker finalizes before timeout") && ok;
	ok = check(
		runner.completion() != nullptr &&
			runner.completion()->taskResult.status ==
				GameLoading::LoadingTaskStatus::Cancelled &&
			runner.completion()->taskResult.message ==
				"fixture cancelled",
		"worker observes cancellation token and returns Cancelled") && ok;
	return ok;
}

bool runQuitLatchTest()
{
	std::promise<void> releaseWorkerPromise;
	std::shared_future<void> releaseWorker =
		releaseWorkerPromise.get_future().share();
	GameLoading::ExclusiveLoadingRunner runner(
		[&](
			const GameLoading::LoadingCancellationToken&)
		{
			releaseWorker.wait();
			return GameLoading::LoadingTaskResult::success();
		});

	bool released = false;
	const auto pump =
		[&](GameLoading::ExclusiveLoadingRunner& activeRunner)
		{
			activeRunner.latchWindowClose();
			activeRunner.latchTerminalQuit();
			if (!released)
			{
				released = true;
				releaseWorkerPromise.set_value();
			}
		};
	bool ok = check(
		[&]()
		{
			const auto deadline =
				std::chrono::steady_clock::now() +
					TestTimeout;
			while (std::chrono::steady_clock::now() <
				deadline)
			{
				if (runner.poll(
						pump,
						{},
						[]()
						{
							return false;
						}) ==
					GameLoading::
						ExclusiveLoadingPollStatus::
							Finalized)
				{
					return true;
				}
				std::this_thread::yield();
			}
			return false;
		}(),
		"latched quit worker finalizes before timeout");
	const GameLoading::ExclusiveLoadingCompletion* completion =
		runner.completion();
	ok = check(
		completion != nullptr &&
			completion->windowCloseRequested &&
			completion->terminalQuitRequested,
		"window close and terminal quit are latched into completion") && ok;
	ok = check(
		!runner.isCancellationRequested(),
		"quit latching does not implicitly change cancellation policy") && ok;
	return ok;
}

bool runDestructorJoinTest()
{
	std::mutex mutex;
	std::condition_variable condition;
	bool allowWorkerExit = false;
	bool workerObservedCancellation = false;
	bool workerExited = false;
	std::promise<void> workerEnteredPromise;
	std::future<void> workerEntered =
		workerEnteredPromise.get_future();

	auto runner =
		std::make_unique<GameLoading::ExclusiveLoadingRunner>(
			[&](
				const GameLoading::LoadingCancellationToken&
					cancellationToken)
			{
				workerEnteredPromise.set_value();
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait(
					lock,
					[&]()
					{
						return allowWorkerExit;
					});
				lock.unlock();
				const auto cancellationDeadline =
					std::chrono::steady_clock::now() +
						TestTimeout;
				while (!cancellationToken.
						isCancellationRequested() &&
					std::chrono::steady_clock::now() <
						cancellationDeadline)
				{
					std::this_thread::yield();
				}
				workerObservedCancellation =
					cancellationToken.
						isCancellationRequested();
				workerExited = true;
				return GameLoading::LoadingTaskResult::
					cancellation();
			});
	if (!check(
		workerEntered.wait_for(TestTimeout) ==
			std::future_status::ready,
		"destructor-join worker starts"))
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			allowWorkerExit = true;
		}
		condition.notify_all();
		return false;
	}

	std::promise<void> destructorStartedPromise;
	std::future<void> destructorStarted =
		destructorStartedPromise.get_future();
	std::promise<void> destructorReturnedPromise;
	std::future<void> destructorReturned =
		destructorReturnedPromise.get_future();
	std::thread destroyer(
		[ownedRunner = std::move(runner),
		 &destructorStartedPromise,
		 &destructorReturnedPromise]() mutable
		{
			destructorStartedPromise.set_value();
			ownedRunner.reset();
			destructorReturnedPromise.set_value();
		});

	bool ok = check(
		destructorStarted.wait_for(TestTimeout) ==
			std::future_status::ready,
		"runner destruction starts");
	ok = check(
		destructorReturned.wait_for(
			std::chrono::seconds(0)) ==
			std::future_status::timeout,
		"runner destructor waits for the active worker") && ok;

	{
		std::lock_guard<std::mutex> lock(mutex);
		allowWorkerExit = true;
	}
	condition.notify_all();
	ok = check(
		destructorReturned.wait_for(TestTimeout) ==
			std::future_status::ready,
		"runner destructor returns after worker exit") && ok;
	destroyer.join();

	ok = check(
		workerExited,
		"worker exits before runner destruction completes") && ok;
	ok = check(
		workerObservedCancellation,
		"runner destructor requests cooperative cancellation") && ok;
	return ok;
}
}

bool runExclusiveLoadingRunnerTests()
{
	bool ok = true;
	ok = runSuccessAndFinalizeOnceTest() && ok;
	ok = runFailureResultTest() && ok;
	ok = runOwnerFinalizationReadinessTest() && ok;
	ok = runWorkerExceptionTest() && ok;
	ok = runCooperativeCancellationTest() && ok;
	ok = runQuitLatchTest() && ok;
	ok = runDestructorJoinTest() && ok;
	return ok;
}

#if defined(JXQY_EXCLUSIVE_LOADING_RUNNER_STANDALONE)
int main()
{
	return runExclusiveLoadingRunnerTests() ? 0 : 1;
}
#endif
