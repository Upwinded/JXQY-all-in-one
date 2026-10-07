#pragma once
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
#include "AutomationProtocol.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace GameplayAutomation
{
class AutomationPipe final
{
public:
	struct Request
	{
		std::int64_t id = 0;
		std::string command;
		Value arguments;
		bool expired = false;
		bool completed = false;
		std::string reply;
	};
	explicit AutomationPipe(const std::string& name);
	~AutomationPipe();
	AutomationPipe(const AutomationPipe&) = delete;
	AutomationPipe& operator=(const AutomationPipe&) = delete;
	std::shared_ptr<Request> take();
	void reply(const std::shared_ptr<Request>& request, const Value& value);
	std::uint64_t takeDisconnected();
	bool isConnected() const { return connected.load(); }
	std::uint64_t connectionId() const { return connectionSequence.load(); }
private:
	void workerMain();
	std::string process(const std::string& line, std::int64_t& previousId);
	std::string name;
	std::mutex mutex;
	std::condition_variable condition;
	std::deque<std::shared_ptr<Request>> requests;
	std::thread worker;
	std::atomic<bool> stopping{false};
	std::atomic<bool> connected{false};
	std::atomic<std::uint64_t> connectionSequence{0};
	std::atomic<std::uint64_t> disconnected{0};
	void* pipeHandle = nullptr;
	void* stopEvent = nullptr;
};
}
#endif
