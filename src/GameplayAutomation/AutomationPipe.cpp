#include "AutomationPipe.h"
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
#include <chrono>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")
#endif

namespace GameplayAutomation
{
AutomationPipe::AutomationPipe(const std::string& sessionName) : name(sessionName)
{
#if defined(_WIN32)
	if (name.empty() || name.size() > 80 || name.find_first_not_of(
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos)
	{
		throw std::runtime_error("invalid_pipe_name");
	}
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		throw std::runtime_error("cannot_read_current_user");
	DWORD size = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &size);
	std::vector<unsigned char> user(size);
	const bool userRead = GetTokenInformation(token, TokenUser, user.data(), size, &size) != FALSE;
	CloseHandle(token);
	if (!userRead) throw std::runtime_error("cannot_read_current_user");
	LPSTR sid = nullptr;
	if (!ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid))
		throw std::runtime_error("cannot_encode_current_user");
	const std::string descriptor = "D:P(A;;GA;;;" + std::string(sid) + ")";
	LocalFree(sid);
	PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(descriptor.c_str(),
		SDDL_REVISION_1, &securityDescriptor, nullptr))
		throw std::runtime_error("cannot_create_pipe_permissions");
	SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), securityDescriptor, FALSE};
	const std::string path = "\\\\.\\pipe\\jxqy-" + name;
	HANDLE pipe = CreateNamedPipeA(path.c_str(),
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
		1, 64 * 1024, 64 * 1024, 5000, &attributes);
	LocalFree(securityDescriptor);
	if (pipe == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot_create_pipe");
	pipeHandle = pipe;
	stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!stopEvent)
	{
		CloseHandle(pipe);
		throw std::runtime_error("cannot_create_stop_event");
	}
	try { worker = std::thread(&AutomationPipe::workerMain, this); }
	catch (...)
	{
		CloseHandle(static_cast<HANDLE>(stopEvent));
		CloseHandle(pipe);
		throw;
	}
#else
	throw std::runtime_error("automation_pipe_requires_windows");
#endif
}

AutomationPipe::~AutomationPipe()
{
	stopping.store(true);
	condition.notify_all();
#if defined(_WIN32)
	SetEvent(static_cast<HANDLE>(stopEvent));
#endif
	if (worker.joinable()) worker.join();
#if defined(_WIN32)
	CloseHandle(static_cast<HANDLE>(stopEvent));
	CloseHandle(static_cast<HANDLE>(pipeHandle));
#endif
}

std::shared_ptr<AutomationPipe::Request> AutomationPipe::take()
{
	std::lock_guard<std::mutex> lock(mutex);
	while (!requests.empty())
	{
		auto request = requests.front();
		requests.pop_front();
		if (!request->expired) return request;
	}
	return {};
}

void AutomationPipe::reply(const std::shared_ptr<Request>& request, const Value& value)
{
	const auto bytes = serialize(value);
	std::lock_guard<std::mutex> lock(mutex);
	if (request->expired || request->completed) return;
	request->reply = bytes;
	request->completed = true;
	condition.notify_all();
}

std::uint64_t AutomationPipe::takeDisconnected()
{
	return disconnected.exchange(0);
}

std::string AutomationPipe::process(const std::string& line, std::int64_t& previousId)
{
	std::int64_t id = 0;
	try
	{
		if (line.size() > MaximumRequestBytes) throw std::runtime_error("request_too_large");
#if defined(_WIN32)
		if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(),
			static_cast<int>(line.size()), nullptr, 0)) throw std::runtime_error("invalid_utf8");
#endif
		Value value;
		StrictJson::StrictJsonParser parser(line);
		if (!parser.parse(value)) throw std::runtime_error("invalid_json: " + parser.message());
		fields(value, {"version", "id", "command", "arguments"});
		if (integer(member(value, "version")) != 1) throw std::runtime_error("unsupported_version");
		id = integer(member(value, "id"));
		if (id <= previousId || id > 9007199254740991LL) throw std::runtime_error("duplicate_or_invalid_id");
		previousId = id;
		auto request = std::make_shared<Request>();
		request->id = id;
		request->command = textMember(value, "command");
		request->arguments = member(value, "arguments");
		if (request->arguments.type != Type::Object) throw std::runtime_error("expected_arguments_object");
		std::unique_lock<std::mutex> lock(mutex);
		if (requests.size() >= 16) throw std::runtime_error("queue_full");
		requests.push_back(request);
		if (!condition.wait_for(lock, std::chrono::seconds(10), [&]()
			{ return request->completed || stopping.load(); }))
		{
			request->expired = true;
			throw std::runtime_error("main_thread_timeout");
		}
		if (stopping.load()) throw std::runtime_error("session_closed");
		return request->reply;
	}
	catch (const std::exception& error)
	{
		return serialize(response(id, false, string(error.what())));
	}
}

void AutomationPipe::workerMain()
{
#if defined(_WIN32)
	const HANDLE pipe = static_cast<HANDLE>(pipeHandle);
	const HANDLE stop = static_cast<HANDLE>(stopEvent);
	// One worker owns all overlapped operations; shutdown always joins it.
	auto operation = [&](bool reading, void* buffer, DWORD length, DWORD& transferred)
	{
		OVERLAPPED overlap{};
		overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!overlap.hEvent) return false;
		BOOL result = reading ? ReadFile(pipe, buffer, length, &transferred, &overlap)
			: WriteFile(pipe, buffer, length, &transferred, &overlap);
		if (!result && GetLastError() == ERROR_IO_PENDING)
		{
			HANDLE events[]{stop, overlap.hEvent};
			if (WaitForMultipleObjects(2, events, FALSE, reading ? 30000 : 5000) == WAIT_OBJECT_0 + 1)
				result = GetOverlappedResult(pipe, &overlap, &transferred, FALSE);
			else
			{
				CancelIoEx(pipe, &overlap);
				GetOverlappedResult(pipe, &overlap, &transferred, TRUE);
				result = FALSE;
			}
		}
		CloseHandle(overlap.hEvent);
		return result != FALSE;
	};
	while (!stopping.load())
	{
		OVERLAPPED overlap{};
		overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!overlap.hEvent) break;
		BOOL accepted = ConnectNamedPipe(pipe, &overlap);
		DWORD error = accepted ? ERROR_SUCCESS : GetLastError();
		if (error == ERROR_PIPE_CONNECTED) accepted = TRUE;
		else if (error == ERROR_IO_PENDING)
		{
			HANDLE events[]{stop, overlap.hEvent};
			DWORD ignored = 0;
			if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
				accepted = GetOverlappedResult(pipe, &overlap, &ignored, FALSE);
			else
			{
				CancelIoEx(pipe, &overlap);
				GetOverlappedResult(pipe, &overlap, &ignored, TRUE);
			}
		}
		CloseHandle(overlap.hEvent);
		if (!accepted || stopping.load()) break;
		++connectionSequence;
		connected.store(true);
		std::int64_t previousId = 0;
		std::string input;
		bool healthy = true;
		while (healthy && !stopping.load())
		{
			char buffer[4096];
			DWORD count = 0;
			if (!operation(true, buffer, sizeof(buffer), count) || count == 0) break;
			input.append(buffer, count);
			while (healthy)
			{
				const auto end = input.find('\n');
				if (end == std::string::npos) break;
				std::string output = process(input.substr(0, end), previousId);
				input.erase(0, end + 1);
				std::size_t sent = 0;
				while (sent < output.size())
				{
					DWORD written = 0;
					if (!operation(false, output.data() + sent,
						static_cast<DWORD>(output.size() - sent), written) || written == 0)
					{
						healthy = false;
						break;
					}
					sent += written;
				}
			}
			if (input.size() > MaximumRequestBytes) break;
		}
		{
			std::lock_guard<std::mutex> lock(mutex);
			for (auto& request : requests) request->expired = true;
			requests.clear();
		}
		disconnected.store(connectionSequence.load());
		connected.store(false);
		DisconnectNamedPipe(pipe);
	}
#endif
}
}
#endif
