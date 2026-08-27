#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace GameLoading
{
enum class LoadingTaskStatus
{
	Succeeded,
	Failed,
	Cancelled
};

struct LoadingTaskResult
{
	LoadingTaskStatus status = LoadingTaskStatus::Failed;
	std::string message;
	std::exception_ptr exception;

	static LoadingTaskResult success();
	static LoadingTaskResult failure(
		std::string message,
		std::exception_ptr exception = {});
	static LoadingTaskResult cancellation(
		std::string message = {});

	bool succeeded() const noexcept;
};

struct ExclusiveLoadingSharedState;

// This token may be copied and retained by cooperating worker operations.
// Cancellation is advisory: workers must check the token at safe boundaries
// and return LoadingTaskResult::cancellation() without committing live state.
class LoadingCancellationToken final
{
public:
	LoadingCancellationToken() = default;

	bool isCancellationRequested() const noexcept;

private:
	explicit LoadingCancellationToken(
		std::shared_ptr<ExclusiveLoadingSharedState> sharedState);

	std::shared_ptr<ExclusiveLoadingSharedState> sharedState;

	friend class ExclusiveLoadingRunner;
};

struct ExclusiveLoadingCompletion
{
	LoadingTaskResult taskResult;
	bool windowCloseRequested = false;
	bool terminalQuitRequested = false;
};

enum class ExclusiveLoadingPollStatus
{
	Running,
	Finalized
};

class ExclusiveLoadingRunner final
{
public:
	using Worker = std::function<LoadingTaskResult(
		const LoadingCancellationToken& cancellationToken)>;
	using PumpCallback = std::function<void(
		ExclusiveLoadingRunner& runner)>;
	using FinalizeCallback = std::function<void(
		const ExclusiveLoadingCompletion& completion)>;
	using OwnerFinalizationReadyCallback =
		std::function<bool()>;

	// Starts worker immediately. An empty worker or thread-start failure is
	// published as a Failed result and can be collected through poll().
	explicit ExclusiveLoadingRunner(Worker worker);
	~ExclusiveLoadingRunner();

	ExclusiveLoadingRunner(const ExclusiveLoadingRunner&) = delete;
	ExclusiveLoadingRunner& operator=(
		const ExclusiveLoadingRunner&) = delete;
	ExclusiveLoadingRunner(ExclusiveLoadingRunner&&) = delete;
	ExclusiveLoadingRunner& operator=(
		ExclusiveLoadingRunner&&) = delete;

	// Owner-thread operation. pump is invoked once per call before completion
	// is checked, allowing the application to keep its event and render loop
	// alive. finalize is invoked at most once, after the worker has been joined
	// and its result has been acquired. A completed worker remains collectable
	// while ownerFinalizationReady returns false. Readiness is evaluated after
	// pump so a lifecycle event observed by that pump cannot be bypassed by a
	// stale pre-pump snapshot. A terminal quit always overrides the gate so
	// cancellation and worker cleanup cannot wait for foreground.
	ExclusiveLoadingPollStatus poll(
		const PumpCallback& pump,
		const FinalizeCallback& finalize,
		const OwnerFinalizationReadyCallback&
			ownerFinalizationReady = {});

	void requestCancellation() noexcept;
	bool isCancellationRequested() const noexcept;

	// Latching does not request cancellation. The caller owns the policy for a
	// confirmable window close versus an unconditional terminal quit.
	void latchWindowClose() noexcept;
	void latchTerminalQuit() noexcept;
	bool isWindowCloseLatched() const noexcept;
	bool isTerminalQuitLatched() const noexcept;

	bool isWorkerComplete() const noexcept;
	bool isFinalized() const noexcept;
	const ExclusiveLoadingCompletion* completion() const noexcept;

private:
	void workerMain(Worker worker) noexcept;
	void joinWorker() noexcept;

	std::shared_ptr<ExclusiveLoadingSharedState> sharedState;
	LoadingTaskResult workerResult;
	ExclusiveLoadingCompletion finalCompletion;
	std::thread workerThread;
	bool finalized = false;
};
}
