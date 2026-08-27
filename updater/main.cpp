#include "DesktopProgramUpdater.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
struct Arguments
{
	std::filesystem::path releaseRoot;
	std::string target;
	std::uint64_t processId = 0;
};

#if defined(_WIN32)
bool parseUnsigned(const std::wstring& text, std::uint64_t& value)
{
	if (text.empty())
	{
		return false;
	}
	wchar_t* end = nullptr;
	errno = 0;
	const unsigned long long parsed = std::wcstoull(text.c_str(), &end, 10);
	if (errno != 0 || end == text.c_str() || *end != L'\0')
	{
		return false;
	}
	value = static_cast<std::uint64_t>(parsed);
	return true;
}

bool parseArguments(int argumentCount, wchar_t** argumentValues,
	Arguments& arguments)
{
	for (int index = 1; index + 1 < argumentCount; index += 2)
	{
		const std::wstring name(argumentValues[index]);
		const std::wstring value(argumentValues[index + 1]);
		if (name == L"--release-root")
		{
			arguments.releaseRoot = value;
		}
		else if (name == L"--target")
		{
			if (value == L"win32")
			{
				arguments.target = "win32";
			}
			else if (value == L"win64")
			{
				arguments.target = "win64";
			}
			else
			{
				return false;
			}
		}
		else if (name == L"--wait-pid")
		{
			if (!parseUnsigned(value, arguments.processId))
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	return argumentCount == 7 && !arguments.releaseRoot.empty() &&
		(arguments.target == "win32" || arguments.target == "win64") &&
		arguments.processId > 0 &&
		arguments.processId <= std::numeric_limits<DWORD>::max();
}

bool waitForProcess(std::uint64_t processId)
{
	HANDLE process = OpenProcess(
		SYNCHRONIZE, FALSE, static_cast<DWORD>(processId));
	if (process == nullptr)
	{
		return GetLastError() == ERROR_INVALID_PARAMETER;
	}
	const DWORD waitResult = WaitForSingleObject(process, INFINITE);
	CloseHandle(process);
	return waitResult == WAIT_OBJECT_0;
}

bool launchProgram(
	const std::filesystem::path& executablePath,
	const std::filesystem::path& workingDirectory)
{
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	const BOOL started = CreateProcessW(
		executablePath.c_str(),
		nullptr,
		nullptr,
		nullptr,
		FALSE,
		CREATE_NEW_PROCESS_GROUP,
		nullptr,
		workingDirectory.c_str(),
		&startup,
		&process);
	if (!started)
	{
		return false;
	}
	CloseHandle(process.hThread);
	const DWORD validationWait = WaitForSingleObject(process.hProcess, 3000);
	bool healthy = validationWait == WAIT_TIMEOUT;
	if (validationWait == WAIT_OBJECT_0)
	{
		DWORD exitCode = 1;
		healthy = GetExitCodeProcess(process.hProcess, &exitCode) &&
			exitCode == 0;
	}
	CloseHandle(process.hProcess);
	return healthy;
}
#else
bool parseUnsigned(const std::string& text, std::uint64_t& value)
{
	if (text.empty())
	{
		return false;
	}
	char* end = nullptr;
	errno = 0;
	const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
	if (errno != 0 || end == text.c_str() || *end != '\0')
	{
		return false;
	}
	value = static_cast<std::uint64_t>(parsed);
	return true;
}

bool parseArguments(int argumentCount, char** argumentValues,
	Arguments& arguments)
{
	for (int index = 1; index + 1 < argumentCount; index += 2)
	{
		const std::string name(argumentValues[index]);
		const std::string value(argumentValues[index + 1]);
		if (name == "--release-root")
		{
			arguments.releaseRoot = std::filesystem::u8path(value);
		}
		else if (name == "--target")
		{
			arguments.target = value;
		}
		else if (name == "--wait-pid")
		{
			if (!parseUnsigned(value, arguments.processId))
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	return argumentCount == 7 && !arguments.releaseRoot.empty() &&
		arguments.target == "linux" && arguments.processId > 0 &&
		arguments.processId <=
			static_cast<std::uint64_t>(std::numeric_limits<pid_t>::max());
}

bool waitForProcess(std::uint64_t processId)
{
	const pid_t process = static_cast<pid_t>(processId);
	for (;;)
	{
		if (kill(process, 0) == 0 || errno == EPERM)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}
		return errno == ESRCH;
	}
}

bool launchProgram(
	const std::filesystem::path& executablePath,
	const std::filesystem::path& workingDirectory)
{
	int errorPipe[2];
	if (pipe(errorPipe) != 0)
	{
		return false;
	}
	if (fcntl(errorPipe[1], F_SETFD, FD_CLOEXEC) != 0)
	{
		close(errorPipe[0]);
		close(errorPipe[1]);
		return false;
	}
	const pid_t child = fork();
	if (child < 0)
	{
		close(errorPipe[0]);
		close(errorPipe[1]);
		return false;
	}
	if (child == 0)
	{
		close(errorPipe[0]);
		if (chdir(workingDirectory.c_str()) != 0)
		{
			const int childError = errno;
			(void)write(errorPipe[1], &childError, sizeof(childError));
			_exit(127);
		}
		const std::string executable = executablePath.string();
		char* const childArguments[] = {
			const_cast<char*>(executable.c_str()), nullptr
		};
		execv(executable.c_str(), childArguments);
		const int childError = errno;
		(void)write(errorPipe[1], &childError, sizeof(childError));
		_exit(127);
	}
	close(errorPipe[1]);
	int childError = 0;
	ssize_t bytesRead = -1;
	do
	{
		bytesRead = read(errorPipe[0], &childError, sizeof(childError));
	}
	while (bytesRead < 0 && errno == EINTR);
	close(errorPipe[0]);
	if (bytesRead != 0)
	{
		return false;
	}
	for (int attempt = 0; attempt < 30; ++attempt)
	{
		int status = 0;
		const pid_t waitResult = waitpid(child, &status, WNOHANG);
		if (waitResult == child)
		{
			return WIFEXITED(status) && WEXITSTATUS(status) == 0;
		}
		if (waitResult < 0)
		{
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	return true;
}
#endif

int runUpdater(const Arguments& arguments)
{
	if (!waitForProcess(arguments.processId))
	{
		std::cerr << "Unable to wait for the game process." << std::endl;
		return 2;
	}
	ProgramUpdate::DesktopProgramUpdateRequest request;
	request.releaseRoot = arguments.releaseRoot;
	request.target = arguments.target;
	const ProgramUpdate::DesktopProgramUpdateResult result =
		ProgramUpdate::applyDesktopProgramUpdate(request, launchProgram);
	if (result.succeeded())
	{
		return 0;
	}
	std::cerr << "Program update failed with status "
		<< static_cast<int>(result.status) << "." << std::endl;
	return 3;
}
}

#if defined(_WIN32)
int wmain(int argumentCount, wchar_t** argumentValues)
#else
int main(int argumentCount, char** argumentValues)
#endif
{
	Arguments arguments;
	if (!parseArguments(argumentCount, argumentValues, arguments))
	{
		std::cerr << "Usage: jxqy-program-updater --release-root <path> "
			"--target <win32|win64|linux> --wait-pid <pid>" << std::endl;
		return 1;
	}
	return runUpdater(arguments);
}
