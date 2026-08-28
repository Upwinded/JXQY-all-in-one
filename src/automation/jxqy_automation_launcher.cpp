#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
#ifndef JXQY_GAME_EXECUTABLE_NAME
#define JXQY_GAME_EXECUTABLE_NAME "jxqy-all-in-one.exe"
#endif

constexpr const wchar_t* DefaultArguments = L"--resource-id XJXQY_TEST_MOD --skip-startup-video --newgame --enable-automation-hooks --log-file automation-smoke.log";
constexpr const char* GameExecutableName = JXQY_GAME_EXECUTABLE_NAME;
constexpr const wchar_t* ConfigFileName = L"jxqy-automation-launcher.ini";

std::string trimAscii(std::string value)
{
	while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r' || value.front() == '\n'))
	{
		value.erase(value.begin());
	}
	while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n'))
	{
		value.pop_back();
	}
	return value;
}

std::wstring decodeUtf8(const std::string& value)
{
	if (value.empty())
	{
		return {};
	}

	int resultLength = MultiByteToWideChar(
		CP_UTF8,
		MB_ERR_INVALID_CHARS,
		value.data(),
		static_cast<int>(value.size()),
		nullptr,
		0);
	if (resultLength <= 0)
	{
		return {};
	}
	std::wstring result(static_cast<size_t>(resultLength), L'\0');
	if (MultiByteToWideChar(
			CP_UTF8,
			MB_ERR_INVALID_CHARS,
			value.data(),
			static_cast<int>(value.size()),
			result.data(),
			resultLength) != resultLength)
	{
		return {};
	}
	return result;
}

std::filesystem::path getExecutableDirectory()
{
	std::vector<wchar_t> buffer(MAX_PATH);
	DWORD len = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
	while (len == buffer.size())
	{
		buffer.resize(buffer.size() * 2);
		len = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
	}
	if (len == 0)
	{
		return std::filesystem::current_path();
	}
	return std::filesystem::path(std::wstring(buffer.data(), len)).parent_path();
}

std::wstring readConfiguredArguments(const std::filesystem::path& configPath)
{
	std::ifstream input(configPath);
	if (!input)
	{
		return DefaultArguments;
	}

	std::string line;
	while (std::getline(input, line))
	{
		if (line.size() >= 3 &&
			static_cast<unsigned char>(line[0]) == 0xEF &&
			static_cast<unsigned char>(line[1]) == 0xBB &&
			static_cast<unsigned char>(line[2]) == 0xBF)
		{
			line.erase(0, 3);
		}
		line = trimAscii(line);
		if (line.empty() || line.front() == ';' || line.front() == '#')
		{
			continue;
		}
		std::wstring decoded = decodeUtf8(line);
		return decoded.empty() ? DefaultArguments : decoded;
	}
	return DefaultArguments;
}

int showLaunchError(const std::wstring& gamePath, DWORD errorCode)
{
	std::wstring message = L"Failed to launch:\n" + gamePath + L"\n\nError code: " + std::to_wstring(errorCode);
	MessageBoxW(nullptr, message.c_str(), L"JXQY Automation Launcher", MB_ICONERROR | MB_OK);
	return (int)errorCode;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	std::filesystem::path executableDirectory = getExecutableDirectory();
	std::filesystem::path gamePath = executableDirectory / GameExecutableName;
	std::wstring arguments = readConfiguredArguments(executableDirectory / ConfigFileName);
	std::wstring commandLine = L"\"" + gamePath.wstring() + L"\" " + arguments;

	STARTUPINFOW startupInfo = {};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInfo = {};
	std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
	commandLineBuffer.push_back(L'\0');

	BOOL launched = CreateProcessW(
		gamePath.c_str(),
		commandLineBuffer.data(),
		nullptr,
		nullptr,
		FALSE,
		0,
		nullptr,
		executableDirectory.c_str(),
		&startupInfo,
		&processInfo);

	if (!launched)
	{
		return showLaunchError(gamePath.wstring(), GetLastError());
	}

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	return 0;
}
