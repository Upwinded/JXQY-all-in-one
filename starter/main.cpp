#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace
{
constexpr wchar_t ApplicationTitle[] = L"剑侠情缘 All-in-One";

#if defined(_WIN64)
constexpr wchar_t PlatformDirectory[] = L"win64";
#else
constexpr wchar_t PlatformDirectory[] = L"win32";
#endif

#if defined(_DEBUG)
constexpr wchar_t GameExecutableName[] = L"jxqy-all-in-one-debug.exe";
#else
constexpr wchar_t GameExecutableName[] = L"jxqy-all-in-one.exe";
#endif

std::filesystem::path getLauncherPath()
{
	std::vector<wchar_t> buffer(32768);
	const DWORD length = GetModuleFileNameW(
		nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
	if (length == 0 || length >= buffer.size())
	{
		return {};
	}
	return std::filesystem::path(
		std::wstring(buffer.data(), length));
}

std::wstring quoteArgument(const std::filesystem::path& path)
{
	return L"\"" + path.native() + L"\"";
}

int showError(const std::wstring& message)
{
	MessageBoxW(
		nullptr,
		message.c_str(),
		ApplicationTitle,
		MB_OK | MB_ICONERROR);
	return 1;
}
}

int WINAPI wWinMain(
	HINSTANCE,
	HINSTANCE,
	PWSTR commandLine,
	int)
{
	const std::filesystem::path launcherPath = getLauncherPath();
	if (launcherPath.empty())
	{
		return showError(L"无法确定启动器所在目录。");
	}

	const std::filesystem::path releaseRoot = launcherPath.parent_path();
	const std::filesystem::path gamePath =
		releaseRoot / L"bin" / PlatformDirectory / GameExecutableName;
	const std::filesystem::path assetsPath = releaseRoot / L"assets";

	std::error_code error;
	if (!std::filesystem::is_regular_file(gamePath, error) || error)
	{
		return showError(
			L"找不到游戏主程序：\n" + gamePath.native());
	}
	error.clear();
	if (!std::filesystem::is_directory(assetsPath, error) || error)
	{
		return showError(
			L"找不到资源目录：\n" + assetsPath.native());
	}

	std::wstring childCommandLine =
		quoteArgument(gamePath) + L" --assets " + quoteArgument(assetsPath);
	if (commandLine != nullptr && commandLine[0] != L'\0')
	{
		childCommandLine += L" ";
		childCommandLine += commandLine;
	}
	std::vector<wchar_t> mutableCommandLine(
		childCommandLine.begin(), childCommandLine.end());
	mutableCommandLine.push_back(L'\0');

	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInfo{};
	if (!CreateProcessW(
			gamePath.c_str(),
			mutableCommandLine.data(),
			nullptr,
			nullptr,
			FALSE,
			0,
			nullptr,
			releaseRoot.c_str(),
			&startupInfo,
			&processInfo))
	{
		return showError(
			L"无法启动游戏主程序，Windows 错误码：" +
			std::to_wstring(GetLastError()));
	}

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	return 0;
}
