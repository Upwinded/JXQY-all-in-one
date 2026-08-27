#include "ExclusiveLoadingRunner.h"

#include <atomic>
#include <exception>
#include <utility>

namespace GameLoading
{
struct ExclusiveLoadingSharedState
{
	std::atomic<bool> cancellationRequested{false};
	std::atomic<bool> windowCloseRequested{false};
	std::atomic<bool> terminalQuitRequested{false};
	std::atomic<bool> workerComplete{false};
};

namespace
{
LoadingTaskResult exceptionFailure(
	const char* fallbackMessage) noexcept
{
	const std::exception_ptr exception =
		std::current_exception();
	LoadingTaskResult result;
	result.status = LoadingTaskStatus::Failed;
	result.exception = exception;
	try
	{
		result.message = fallbackMessage;
		if (!exception)
		{
			return result;
		}
		std::rethrow_exception(exception);
	}
	catch (const std::exception& error)
	{
		try
		{
			result.message = error.what();
		}
		catch (...)
		{
		}
	}
	catch (...)
	{
	}
	return result;
}
}

LoadingTaskResult LoadingTaskResult::success()
{
	LoadingTaskResult result;
	result.status = LoadingTaskStatus::Succeeded;
	return result;
}

LoadingTaskResult LoadingTaskResult::failure(
	std::string resultMessage,
	std::exception_ptr resultException)
{
	LoadingTaskResult result;
	result.status = LoadingTaskStatus::Failed;
	result.message = std::move(resultMessage);
	result.exception = std::move(resultException);
	return result;
}

LoadingTaskResult LoadingTaskResult::cancellation(
	std::string resultMessage)
{
	LoadingTaskResult result;
	result.status = LoadingTaskStatus::Cancelled;
	result.message = std::move(resultMessage);
	return result;
}

bool LoadingTaskResult::succeeded() const noexcept
{
	return status == LoadingTaskStatus::Succeeded;
}

LoadingCancellationToken::LoadingCancellationToken(
	std::shared_ptr<ExclusiveLoadingSharedState>
		tokenSharedState) :
	sharedState(std::move(tokenSharedState))
{
}

bool LoadingCancellationToken::
	isCancellationRequested() const noexcept
{
	return sharedState &&
		sharedState->cancellationRequested.load(
			std::memory_order_acquire);
}

ExclusiveLoadingRunner::ExclusiveLoadingRunner(
	Worker worker) :
	sharedState(
		std::make_shared<ExclusiveLoadingSharedState>())
{
	if (!worker)
	{
		workerResult = LoadingTaskResult::failure(
			"Exclusive loading worker is not configured.");
		sharedState->workerComplete.store(
			true,
			std::memory_order_release);
		return;
	}

	try
	{
		workerThread = std::thread(
			[this, worker = std::move(worker)]() mutable
			{
				workerMain(std::move(worker));
			});
	}
	catch (...)
	{
		workerResult = exceptionFailure(
			"Failed to start exclusive loading worker.");
		sharedState->workerComplete.store(
			true,
			std::memory_order_release);
	}
}

ExclusiveLoadingRunner::~ExclusiveLoadingRunner()
{
	requestCancellation();
	joinWorker();
}

ExclusiveLoadingPollStatus ExclusiveLoadingRunner::poll(
	const PumpCallback& pump,
	const FinalizeCallback& finalize,
	const OwnerFinalizationReadyCallback&
		ownerFinalizationReady)
{
	if (finalized)
	{
		return ExclusiveLoadingPollStatus::Finalized;
	}

	if (pump)
	{
		pump(*this);
	}

	if (!isWorkerComplete())
	{
		return ExclusiveLoadingPollStatus::Running;
	}
	if (!isTerminalQuitLatched() &&
		ownerFinalizationReady &&
		!ownerFinalizationReady())
	{
		return ExclusiveLoadingPollStatus::Running;
	}

	joinWorker();
	finalCompletion.taskResult = std::move(workerResult);
	finalCompletion.windowCloseRequested =
		isWindowCloseLatched();
	finalCompletion.terminalQuitRequested =
		isTerminalQuitLatched();
	finalized = true;

	if (finalize)
	{
		finalize(finalCompletion);
	}
	return ExclusiveLoadingPollStatus::Finalized;
}

void ExclusiveLoadingRunner::
	requestCancellation() noexcept
{
	sharedState->cancellationRequested.store(
		true,
		std::memory_order_release);
}

bool ExclusiveLoadingRunner::
	isCancellationRequested() const noexcept
{
	return sharedState->cancellationRequested.load(
		std::memory_order_acquire);
}

void ExclusiveLoadingRunner::latchWindowClose() noexcept
{
	sharedState->windowCloseRequested.store(
		true,
		std::memory_order_release);
}

void ExclusiveLoadingRunner::latchTerminalQuit() noexcept
{
	sharedState->terminalQuitRequested.store(
		true,
		std::memory_order_release);
}

bool ExclusiveLoadingRunner::
	isWindowCloseLatched() const noexcept
{
	return sharedState->windowCloseRequested.load(
		std::memory_order_acquire);
}

bool ExclusiveLoadingRunner::
	isTerminalQuitLatched() const noexcept
{
	return sharedState->terminalQuitRequested.load(
		std::memory_order_acquire);
}

bool ExclusiveLoadingRunner::
	isWorkerComplete() const noexcept
{
	return sharedState->workerComplete.load(
		std::memory_order_acquire);
}

bool ExclusiveLoadingRunner::isFinalized() const noexcept
{
	return finalized;
}

const ExclusiveLoadingCompletion*
ExclusiveLoadingRunner::completion() const noexcept
{
	return finalized ? &finalCompletion : nullptr;
}

void ExclusiveLoadingRunner::workerMain(
	Worker worker) noexcept
{
	try
	{
		const LoadingCancellationToken cancellationToken(
			sharedState);
		workerResult = worker(cancellationToken);
	}
	catch (...)
	{
		workerResult = exceptionFailure(
			"Exclusive loading worker failed.");
	}
	sharedState->workerComplete.store(
		true,
		std::memory_order_release);
}

void ExclusiveLoadingRunner::joinWorker() noexcept
{
	if (workerThread.joinable())
	{
		workerThread.join();
	}
}
}
