#include "RootedResourceReader.h"

#include "StrictRelativeResourcePath.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace RootedResourceReader
{
struct RootAnchorState
{
	std::filesystem::path openedPath;
	EditorRun::DirectoryIdentity openedIdentity;
#ifdef _WIN32
	HANDLE handle = INVALID_HANDLE_VALUE;
#else
	int descriptor = -1;
#endif

	~RootAnchorState()
	{
#ifdef _WIN32
		if (handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(handle);
		}
#else
		if (descriptor >= 0)
		{
			close(descriptor);
		}
#endif
	}
};
}

namespace
{
#if defined(JXQY_ENABLE_TEST_HOOKS)
std::mutex g_readTestHookMutex;
RootedResourceReader::ReadTestHook g_readTestHook;

void invokeReadTestHook(
	RootedResourceReader::ReadTestPhase phase)
{
	RootedResourceReader::ReadTestHook hook;
	{
		std::lock_guard<std::mutex> lock(
			g_readTestHookMutex);
		hook = g_readTestHook;
	}
	if (hook)
	{
		hook(phase);
	}
}
#endif

RootedResourceReader::Status currentPathStatus(
	const std::filesystem::path& root,
	const ResourcePathSafety::StrictRelativePathResult& relativePath,
	std::filesystem::path& filePath)
{
	std::error_code error;
	const std::filesystem::file_status rootStatus =
		std::filesystem::status(root, error);
	if (error || !std::filesystem::is_directory(rootStatus))
	{
		return RootedResourceReader::Status::InvalidRoot;
	}

	filePath =
		(root /
		 std::filesystem::u8path(relativePath.normalizedPath)).
			lexically_normal();
	const std::filesystem::file_status fileStatus =
		std::filesystem::status(filePath, error);
	if (error)
	{
		return error ==
				std::errc::no_such_file_or_directory
			? RootedResourceReader::Status::NotFound
			: RootedResourceReader::Status::ReadFailed;
	}
	if (!std::filesystem::exists(fileStatus))
	{
		return RootedResourceReader::Status::NotFound;
	}
	if (!std::filesystem::is_regular_file(fileStatus))
	{
		return RootedResourceReader::Status::NotRegularFile;
	}
	return RootedResourceReader::Status::Success;
}

RootedResourceReader::ProbeResult probeRegularFileAtCurrentPath(
	const std::filesystem::path& root,
	const ResourcePathSafety::StrictRelativePathResult& relativePath)
{
	RootedResourceReader::ProbeResult result;
	std::filesystem::path filePath;
	result.status =
		currentPathStatus(root, relativePath, filePath);
	return result;
}

RootedResourceReader::Result readBoundedFileAtCurrentPath(
	const std::filesystem::path& root,
	const ResourcePathSafety::StrictRelativePathResult& relativePath,
	std::size_t maximumBytes)
{
	RootedResourceReader::Result result;
	std::filesystem::path filePath;
	result.status =
		currentPathStatus(root, relativePath, filePath);
	if (result.status != RootedResourceReader::Status::Success)
	{
		return result;
	}

	std::error_code error;
	const std::uintmax_t initialSize =
		std::filesystem::file_size(filePath, error);
	if (error)
	{
		result.status = RootedResourceReader::Status::ReadFailed;
		return result;
	}
	if (initialSize >
		static_cast<std::uintmax_t>(maximumBytes))
	{
		result.status = RootedResourceReader::Status::TooLarge;
		return result;
	}

	std::ifstream input(filePath, std::ios::binary);
	if (!input)
	{
		result.status = RootedResourceReader::Status::ReadFailed;
		return result;
	}
	result.bytes.reserve(
		static_cast<std::size_t>(initialSize));
	std::array<char, 64 * 1024> buffer{};
	while (input)
	{
		input.read(
			buffer.data(),
			static_cast<std::streamsize>(buffer.size()));
		const std::streamsize count = input.gcount();
		if (count <= 0)
		{
			break;
		}
		const std::size_t byteCount =
			static_cast<std::size_t>(count);
		if (byteCount > maximumBytes - result.bytes.size())
		{
			result = {};
			result.status = RootedResourceReader::Status::TooLarge;
			return result;
		}
		result.bytes.insert(
			result.bytes.end(),
			reinterpret_cast<const std::uint8_t*>(
				buffer.data()),
			reinterpret_cast<const std::uint8_t*>(
				buffer.data()) +
				byteCount);
	}
	if (input.bad())
	{
		result = {};
		result.status = RootedResourceReader::Status::ReadFailed;
		return result;
	}
	input.close();
#if defined(JXQY_ENABLE_TEST_HOOKS)
	invokeReadTestHook(
		RootedResourceReader::ReadTestPhase::AfterRead);
#endif
	result.status = RootedResourceReader::Status::Success;
	return result;
}

std::vector<std::string> splitPath(const std::string& normalizedPath)
{
	std::vector<std::string> components;
	std::size_t start = 0;
	while (start < normalizedPath.size())
	{
		const std::size_t separator = normalizedPath.find('/', start);
		components.push_back(separator == std::string::npos
			? normalizedPath.substr(start)
			: normalizedPath.substr(start, separator - start));
		if (separator == std::string::npos)
		{
			break;
		}
		start = separator + 1;
	}
	return components;
}

std::filesystem::path normalizeRootDirectoryPath(
	const std::filesystem::path& requestedPath)
{
	std::filesystem::path path = requestedPath.lexically_normal();
	while (path != path.root_path() && path.filename().empty())
	{
		path = path.parent_path();
	}
	return path;
}

#ifdef _WIN32
class UniqueHandle
{
public:
	explicit UniqueHandle(HANDLE handleValue = INVALID_HANDLE_VALUE)
		: handle(handleValue)
	{
	}

	~UniqueHandle()
	{
		if (handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(handle);
		}
	}

	UniqueHandle(const UniqueHandle&) = delete;
	UniqueHandle& operator=(const UniqueHandle&) = delete;

	HANDLE get() const
	{
		return handle;
	}

	bool valid() const
	{
		return handle != INVALID_HANDLE_VALUE;
	}

	void reset(HANDLE handleValue = INVALID_HANDLE_VALUE)
	{
		if (handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(handle);
		}
		handle = handleValue;
	}

	HANDLE release()
	{
		const HANDLE released = handle;
		handle = INVALID_HANDLE_VALUE;
		return released;
	}

private:
	HANDLE handle;
};

RootedResourceReader::Status statusForWindowsOpenError(DWORD error)
{
	if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
	{
		return RootedResourceReader::Status::NotFound;
	}
	return RootedResourceReader::Status::ReadFailed;
}

RootedResourceReader::Status statusForNtOpenError(NTSTATUS status)
{
	constexpr NTSTATUS StatusFileIsDirectory =
		static_cast<NTSTATUS>(0xC00000BAUL);
	constexpr NTSTATUS StatusNotDirectory =
		static_cast<NTSTATUS>(0xC0000103UL);
	if (status == StatusFileIsDirectory ||
		status == StatusNotDirectory)
	{
		return RootedResourceReader::Status::NotRegularFile;
	}

	using RtlNtStatusToDosErrorFunction = ULONG(WINAPI*)(NTSTATUS);
	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	const auto convertStatus = ntdll != nullptr
		? reinterpret_cast<RtlNtStatusToDosErrorFunction>(
			GetProcAddress(ntdll, "RtlNtStatusToDosError"))
		: nullptr;
	return convertStatus != nullptr
		? statusForWindowsOpenError(convertStatus(status))
		: RootedResourceReader::Status::ReadFailed;
}

RootedResourceReader::Status openRelativeNoFollow(
	HANDLE parent,
	const std::string& component,
	bool directory,
	UniqueHandle& opened)
{
	using NtCreateFileFunction = decltype(&NtCreateFile);
	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	const NtCreateFileFunction ntCreateFile = ntdll != nullptr
		? reinterpret_cast<NtCreateFileFunction>(
			GetProcAddress(ntdll, "NtCreateFile"))
		: nullptr;
	if (ntCreateFile == nullptr || parent == INVALID_HANDLE_VALUE ||
		component.empty())
	{
		return RootedResourceReader::Status::ReadFailed;
	}

	const std::wstring wideComponent =
		std::filesystem::u8path(component).wstring();
	if (wideComponent.empty() ||
		wideComponent.size() >
			static_cast<std::size_t>(
				(std::numeric_limits<USHORT>::max)()) /
				sizeof(wchar_t))
	{
		return RootedResourceReader::Status::ReadFailed;
	}

	UNICODE_STRING name = {};
	name.Buffer = const_cast<PWSTR>(wideComponent.data());
	name.Length = static_cast<USHORT>(
		wideComponent.size() * sizeof(wchar_t));
	name.MaximumLength = name.Length;
	OBJECT_ATTRIBUTES attributes = {};
	InitializeObjectAttributes(
		&attributes,
		&name,
		OBJ_CASE_INSENSITIVE,
		parent,
		nullptr);

	IO_STATUS_BLOCK ioStatus = {};
	HANDLE handle = INVALID_HANDLE_VALUE;
	const ACCESS_MASK access = FILE_READ_ATTRIBUTES | SYNCHRONIZE |
		(directory ? FILE_LIST_DIRECTORY : FILE_READ_DATA);
	const ULONG options = FILE_OPEN_REPARSE_POINT |
		FILE_SYNCHRONOUS_IO_NONALERT |
		(directory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE);
	const NTSTATUS status = ntCreateFile(
		&handle,
		access,
		&attributes,
		&ioStatus,
		nullptr,
		FILE_ATTRIBUTE_NORMAL,
		directory
			? FILE_SHARE_READ | FILE_SHARE_WRITE
			: FILE_SHARE_READ,
		FILE_OPEN,
		options,
		nullptr,
		0);
	if (status < 0 || handle == INVALID_HANDLE_VALUE)
	{
		if (handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(handle);
		}
		return statusForNtOpenError(status);
	}

	BY_HANDLE_FILE_INFORMATION information = {};
	if (!GetFileInformationByHandle(handle, &information) ||
		GetFileType(handle) != FILE_TYPE_DISK ||
		(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
		(directory
			? (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
			: (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
	{
		CloseHandle(handle);
		return (information.dwFileAttributes &
				FILE_ATTRIBUTE_REPARSE_POINT) != 0
			? RootedResourceReader::Status::EscapesRoot
			: RootedResourceReader::Status::NotRegularFile;
	}
	opened.reset(handle);
	return RootedResourceReader::Status::Success;
}

RootedResourceReader::Status openAbsoluteDirectoryNoFollow(
	const std::filesystem::path& requestedPath,
	UniqueHandle& opened)
{
	opened.reset();
	if (requestedPath.empty() ||
		!requestedPath.is_absolute())
	{
		return RootedResourceReader::Status::InvalidRoot;
	}

	const std::filesystem::path path =
		normalizeRootDirectoryPath(requestedPath);
	const std::filesystem::path rootPath =
		path.root_path();
	if (rootPath.empty())
	{
		return RootedResourceReader::Status::InvalidRoot;
	}

	UniqueHandle current(CreateFileW(
		rootPath.c_str(),
		FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES |
			SYNCHRONIZE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS |
			FILE_FLAG_OPEN_REPARSE_POINT,
		nullptr));
	BY_HANDLE_FILE_INFORMATION rootInformation = {};
	if (!current.valid() ||
		!GetFileInformationByHandle(
			current.get(), &rootInformation) ||
		(rootInformation.dwFileAttributes &
			FILE_ATTRIBUTE_DIRECTORY) == 0 ||
		(rootInformation.dwFileAttributes &
			FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
		GetFileType(current.get()) != FILE_TYPE_DISK)
	{
		return RootedResourceReader::Status::InvalidRoot;
	}

	for (const std::filesystem::path& component :
		path.relative_path())
	{
		const auto componentBytes =
			component.generic_u8string();
		const std::string componentUtf8(
			componentBytes.begin(),
			componentBytes.end());
		if (componentUtf8.empty() ||
			componentUtf8 == "." ||
			componentUtf8 == "..")
		{
			return RootedResourceReader::Status::InvalidRoot;
		}
		UniqueHandle next;
		const RootedResourceReader::Status status =
			openRelativeNoFollow(
				current.get(),
				componentUtf8,
				true,
				next);
		if (status !=
			RootedResourceReader::Status::Success)
		{
			return RootedResourceReader::Status::InvalidRoot;
		}
		current.reset(next.release());
	}

	opened.reset(current.release());
	return RootedResourceReader::Status::Success;
}

RootedResourceReader::Status inspectOpenedHandle(HANDLE handle,
	BY_HANDLE_FILE_INFORMATION& information)
{
	information = {};
	if (!GetFileInformationByHandle(handle, &information))
	{
		return RootedResourceReader::Status::ReadFailed;
	}
	if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
		GetFileType(handle) != FILE_TYPE_DISK)
	{
		return RootedResourceReader::Status::NotRegularFile;
	}
	if (information.nNumberOfLinks != 1)
	{
		return RootedResourceReader::Status::EscapesRoot;
	}
	return RootedResourceReader::Status::Success;
}

RootedResourceReader::Result readFromOpenedHandle(HANDLE handle,
	std::size_t maximumBytes)
{
	RootedResourceReader::Result result;
	BY_HANDLE_FILE_INFORMATION information = {};
	result.status = inspectOpenedHandle(handle, information);
	if (result.status != RootedResourceReader::Status::Success)
	{
		return result;
	}

	ULARGE_INTEGER fileSize = {};
	fileSize.HighPart = information.nFileSizeHigh;
	fileSize.LowPart = information.nFileSizeLow;
	if (fileSize.QuadPart > maximumBytes ||
		fileSize.QuadPart > std::numeric_limits<std::size_t>::max())
	{
		result.status = RootedResourceReader::Status::TooLarge;
		return result;
	}

	result.bytes.resize(static_cast<std::size_t>(fileSize.QuadPart));
	std::size_t offset = 0;
	while (offset < result.bytes.size())
	{
		const std::size_t remaining = result.bytes.size() - offset;
		const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
			remaining, std::numeric_limits<DWORD>::max()));
		DWORD bytesRead = 0;
		if (!ReadFile(handle, result.bytes.data() + offset,
			requested, &bytesRead, nullptr) || bytesRead == 0)
		{
			result.bytes.clear();
			result.status = RootedResourceReader::Status::ReadFailed;
			return result;
		}
		offset += bytesRead;
	}

#if defined(JXQY_ENABLE_TEST_HOOKS)
	invokeReadTestHook(
		RootedResourceReader::ReadTestPhase::AfterRead);
#endif
	BY_HANDLE_FILE_INFORMATION finalInformation = {};
	if (inspectOpenedHandle(
			handle, finalInformation) !=
			RootedResourceReader::Status::Success ||
		information.dwVolumeSerialNumber !=
			finalInformation.dwVolumeSerialNumber ||
		information.nFileIndexHigh !=
			finalInformation.nFileIndexHigh ||
		information.nFileIndexLow !=
			finalInformation.nFileIndexLow ||
		information.nNumberOfLinks !=
			finalInformation.nNumberOfLinks ||
		information.nFileSizeHigh !=
			finalInformation.nFileSizeHigh ||
		information.nFileSizeLow !=
			finalInformation.nFileSizeLow ||
		information.dwFileAttributes !=
			finalInformation.dwFileAttributes ||
		CompareFileTime(
			&information.ftLastWriteTime,
			&finalInformation.ftLastWriteTime) != 0)
	{
		result.bytes.clear();
		result.status =
			RootedResourceReader::Status::ReadFailed;
		return result;
	}

	result.status = RootedResourceReader::Status::Success;
	return result;
}

RootedResourceReader::ProbeResult probeRegularFileFromHandle(
	HANDLE rootHandle,
	const ResourcePathSafety::StrictRelativePathResult& relativePath)
{
	RootedResourceReader::ProbeResult result;
	if (rootHandle == INVALID_HANDLE_VALUE)
	{
		result.status = RootedResourceReader::Status::InvalidRoot;
		return result;
	}

	const std::vector<std::string> components =
		splitPath(relativePath.normalizedPath);
	UniqueHandle currentDirectory;
	HANDLE parent = rootHandle;
	for (std::size_t index = 0; index + 1 < components.size(); ++index)
	{
		UniqueHandle nextDirectory;
		result.status = openRelativeNoFollow(
			parent, components[index], true, nextDirectory);
		if (result.status != RootedResourceReader::Status::Success)
		{
			return result;
		}
		currentDirectory.reset(nextDirectory.release());
		parent = currentDirectory.get();
	}

	UniqueHandle targetHandle;
	result.status = openRelativeNoFollow(
		parent, components.back(), false, targetHandle);
	if (result.status != RootedResourceReader::Status::Success)
	{
		return result;
	}

	BY_HANDLE_FILE_INFORMATION information = {};
	result.status = inspectOpenedHandle(targetHandle.get(), information);
	return result;
}

RootedResourceReader::Result readBoundedFileFromHandle(
	HANDLE rootHandle,
	const ResourcePathSafety::StrictRelativePathResult& relativePath,
	std::size_t maximumBytes)
{
	RootedResourceReader::Result result;
	if (rootHandle == INVALID_HANDLE_VALUE)
	{
		result.status = RootedResourceReader::Status::InvalidRoot;
		return result;
	}

	const std::vector<std::string> components =
		splitPath(relativePath.normalizedPath);
	UniqueHandle currentDirectory;
	HANDLE parent = rootHandle;
	for (std::size_t index = 0; index + 1 < components.size(); ++index)
	{
		UniqueHandle nextDirectory;
		result.status = openRelativeNoFollow(
			parent, components[index], true, nextDirectory);
		if (result.status != RootedResourceReader::Status::Success)
		{
			return result;
		}
		currentDirectory.reset(nextDirectory.release());
		parent = currentDirectory.get();
	}

	UniqueHandle targetHandle;
	result.status = openRelativeNoFollow(
		parent, components.back(), false, targetHandle);
	if (result.status != RootedResourceReader::Status::Success)
	{
		return result;
	}

	return readFromOpenedHandle(targetHandle.get(), maximumBytes);
}

#else
class UniqueFileDescriptor
{
public:
	explicit UniqueFileDescriptor(int descriptorValue = -1)
		: descriptor(descriptorValue)
	{
	}

	~UniqueFileDescriptor()
	{
		if (descriptor >= 0)
		{
			close(descriptor);
		}
	}

	UniqueFileDescriptor(const UniqueFileDescriptor&) = delete;
	UniqueFileDescriptor& operator=(const UniqueFileDescriptor&) = delete;

	int get() const
	{
		return descriptor;
	}

	bool valid() const
	{
		return descriptor >= 0;
	}

	void reset(int descriptorValue)
	{
		if (descriptor >= 0)
		{
			close(descriptor);
		}
		descriptor = descriptorValue;
	}

	int release()
	{
		const int released = descriptor;
		descriptor = -1;
		return released;
	}

private:
	int descriptor;
};

RootedResourceReader::Status statusForOpenAtError(
	int parentDescriptor, const std::string& component, int error)
{
	if (error == ENOENT)
	{
		return RootedResourceReader::Status::NotFound;
	}
	if (error == ELOOP)
	{
		return RootedResourceReader::Status::EscapesRoot;
	}
	if (error == ENOTDIR)
	{
		struct stat information = {};
		if (fstatat(parentDescriptor, component.c_str(), &information,
			AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(information.st_mode))
		{
			return RootedResourceReader::Status::EscapesRoot;
		}
		return RootedResourceReader::Status::NotRegularFile;
	}
	return RootedResourceReader::Status::ReadFailed;
}

RootedResourceReader::Status openAbsoluteDirectoryNoFollow(
	const std::filesystem::path& requestedPath,
	UniqueFileDescriptor& opened)
{
	opened.reset(-1);
	if (requestedPath.empty() ||
		!requestedPath.is_absolute())
	{
		return RootedResourceReader::Status::InvalidRoot;
	}

	const std::filesystem::path path =
		normalizeRootDirectoryPath(requestedPath);
	const std::filesystem::path rootPath =
		path.root_path();
	if (rootPath.empty())
	{
		return RootedResourceReader::Status::InvalidRoot;
	}

	int directoryFlags =
		O_RDONLY | O_DIRECTORY | O_CLOEXEC;
#if defined(O_NOFOLLOW)
	directoryFlags |= O_NOFOLLOW;
#endif
	UniqueFileDescriptor current(
		open(rootPath.c_str(), directoryFlags));
	if (!current.valid())
	{
		return RootedResourceReader::Status::InvalidRoot;
	}
	struct stat rootInformation = {};
	if (fstat(current.get(), &rootInformation) != 0 ||
		!S_ISDIR(rootInformation.st_mode))
	{
		return RootedResourceReader::Status::InvalidRoot;
	}

	for (const std::filesystem::path& component :
		path.relative_path())
	{
		const std::string componentValue =
			component.native();
		if (componentValue.empty() ||
			componentValue == "." ||
			componentValue == "..")
		{
			return RootedResourceReader::Status::InvalidRoot;
		}
		const int nextDescriptor =
			openat(
				current.get(),
				componentValue.c_str(),
				directoryFlags);
		if (nextDescriptor < 0)
		{
			return RootedResourceReader::Status::InvalidRoot;
		}
		current.reset(nextDescriptor);
	}

	opened.reset(current.release());
	return RootedResourceReader::Status::Success;
}

RootedResourceReader::Result readFromOpenedDescriptor(
	int descriptor, std::size_t maximumBytes)
{
	RootedResourceReader::Result result;
	struct stat information = {};
	if (fstat(descriptor, &information) != 0)
	{
		result.status = RootedResourceReader::Status::ReadFailed;
		return result;
	}
	if (!S_ISREG(information.st_mode))
	{
		result.status = RootedResourceReader::Status::NotRegularFile;
		return result;
	}
	if (information.st_nlink != 1)
	{
		result.status = RootedResourceReader::Status::EscapesRoot;
		return result;
	}
	if (information.st_size < 0)
	{
		result.status = RootedResourceReader::Status::ReadFailed;
		return result;
	}

	const std::uintmax_t fileSize =
		static_cast<std::uintmax_t>(information.st_size);
	if (fileSize > maximumBytes ||
		fileSize > std::numeric_limits<std::size_t>::max())
	{
		result.status = RootedResourceReader::Status::TooLarge;
		return result;
	}

	result.bytes.resize(static_cast<std::size_t>(fileSize));
	std::size_t offset = 0;
	while (offset < result.bytes.size())
	{
		const std::size_t remaining = result.bytes.size() - offset;
		const std::size_t requested = std::min<std::size_t>(
			remaining,
			static_cast<std::size_t>(
				std::numeric_limits<ssize_t>::max()));
		const ssize_t bytesRead =
			read(descriptor, result.bytes.data() + offset, requested);
		if (bytesRead < 0 && errno == EINTR)
		{
			continue;
		}
		if (bytesRead <= 0)
		{
			result.bytes.clear();
			result.status = RootedResourceReader::Status::ReadFailed;
			return result;
		}
		offset += static_cast<std::size_t>(bytesRead);
	}

#if defined(JXQY_ENABLE_TEST_HOOKS)
	invokeReadTestHook(
		RootedResourceReader::ReadTestPhase::AfterRead);
#endif
	struct stat finalInformation = {};
	if (fstat(descriptor, &finalInformation) != 0 ||
		information.st_dev != finalInformation.st_dev ||
		information.st_ino != finalInformation.st_ino ||
		information.st_mode != finalInformation.st_mode ||
		information.st_nlink != finalInformation.st_nlink ||
		information.st_size != finalInformation.st_size ||
#if defined(__APPLE__)
		information.st_mtimespec.tv_sec !=
			finalInformation.st_mtimespec.tv_sec ||
		information.st_mtimespec.tv_nsec !=
			finalInformation.st_mtimespec.tv_nsec ||
		information.st_ctimespec.tv_sec !=
			finalInformation.st_ctimespec.tv_sec ||
		information.st_ctimespec.tv_nsec !=
			finalInformation.st_ctimespec.tv_nsec)
#else
		information.st_mtim.tv_sec !=
			finalInformation.st_mtim.tv_sec ||
		information.st_mtim.tv_nsec !=
			finalInformation.st_mtim.tv_nsec ||
		information.st_ctim.tv_sec !=
			finalInformation.st_ctim.tv_sec ||
		information.st_ctim.tv_nsec !=
			finalInformation.st_ctim.tv_nsec)
#endif
	{
		result.bytes.clear();
		result.status =
			RootedResourceReader::Status::ReadFailed;
		return result;
	}

	result.status = RootedResourceReader::Status::Success;
	return result;
}

RootedResourceReader::ProbeResult probeRegularFileFromDescriptor(
	int rootDescriptor,
	const ResourcePathSafety::StrictRelativePathResult& relativePath)
{
	RootedResourceReader::ProbeResult result;
	if (rootDescriptor < 0)
	{
		result.status = RootedResourceReader::Status::InvalidRoot;
		return result;
	}

	UniqueFileDescriptor currentDirectory(
		dup(rootDescriptor));
	if (!currentDirectory.valid())
	{
		result.status = RootedResourceReader::Status::InvalidRoot;
		return result;
	}

	const std::vector<std::string> components =
		splitPath(relativePath.normalizedPath);
	for (std::size_t index = 0; index + 1 < components.size(); ++index)
	{
		const int nextDescriptor = openat(currentDirectory.get(),
			components[index].c_str(),
			O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (nextDescriptor < 0)
		{
			result.status = statusForOpenAtError(currentDirectory.get(),
				components[index], errno);
			return result;
		}
		currentDirectory.reset(nextDescriptor);
	}

	const std::string& fileName = components.back();
	UniqueFileDescriptor fileDescriptor(openat(currentDirectory.get(),
		fileName.c_str(),
		O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
	if (!fileDescriptor.valid())
	{
		result.status = statusForOpenAtError(currentDirectory.get(),
			fileName, errno);
		return result;
	}

	struct stat information = {};
	if (fstat(fileDescriptor.get(), &information) != 0)
	{
		result.status = RootedResourceReader::Status::ReadFailed;
	}
	else
	{
		result.status = !S_ISREG(information.st_mode)
			? RootedResourceReader::Status::NotRegularFile
			: information.st_nlink != 1
				? RootedResourceReader::Status::EscapesRoot
				: RootedResourceReader::Status::Success;
	}
	return result;
}

RootedResourceReader::Result readBoundedFileFromDescriptor(
	int rootDescriptor,
	const ResourcePathSafety::StrictRelativePathResult& relativePath,
	std::size_t maximumBytes)
{
	RootedResourceReader::Result result;
	if (rootDescriptor < 0)
	{
		result.status = RootedResourceReader::Status::InvalidRoot;
		return result;
	}

	UniqueFileDescriptor currentDirectory(
		dup(rootDescriptor));
	if (!currentDirectory.valid())
	{
		result.status = RootedResourceReader::Status::InvalidRoot;
		return result;
	}

	const std::vector<std::string> components =
		splitPath(relativePath.normalizedPath);
	for (std::size_t index = 0; index + 1 < components.size(); ++index)
	{
		const int nextDescriptor = openat(currentDirectory.get(),
			components[index].c_str(),
			O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (nextDescriptor < 0)
		{
			result.status = statusForOpenAtError(currentDirectory.get(),
				components[index], errno);
			return result;
		}
		currentDirectory.reset(nextDescriptor);
	}

	const std::string& fileName = components.back();
	UniqueFileDescriptor fileDescriptor(openat(currentDirectory.get(),
		fileName.c_str(),
		O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
	if (!fileDescriptor.valid())
	{
		result.status = statusForOpenAtError(currentDirectory.get(),
			fileName, errno);
		return result;
	}
	return readFromOpenedDescriptor(fileDescriptor.get(), maximumBytes);
}

#endif

struct OpenedRootAnchorState
{
	RootedResourceReader::Status status =
		RootedResourceReader::Status::InvalidRoot;
	std::shared_ptr<RootedResourceReader::RootAnchorState> state;
};

OpenedRootAnchorState openRootAnchorState(
	const std::filesystem::path& root)
{
	OpenedRootAnchorState result;
#ifdef _WIN32
	UniqueHandle opened;
	result.status =
		openAbsoluteDirectoryNoFollow(root, opened);
	if (result.status !=
		RootedResourceReader::Status::Success)
	{
		return result;
	}
	BY_HANDLE_FILE_INFORMATION information = {};
	if (!GetFileInformationByHandle(
			opened.get(), &information) ||
		GetFileType(opened.get()) != FILE_TYPE_DISK ||
		(information.dwFileAttributes &
			FILE_ATTRIBUTE_DIRECTORY) == 0 ||
		(information.dwFileAttributes &
			FILE_ATTRIBUTE_REPARSE_POINT) != 0)
	{
		result.status =
			RootedResourceReader::Status::InvalidRoot;
		return result;
	}
	auto state = std::make_shared<
		RootedResourceReader::RootAnchorState>();
	state->openedPath = normalizeRootDirectoryPath(root);
	FILE_ID_INFO fileIdInformation = {};
	if (GetFileInformationByHandleEx(
			opened.get(),
			FileIdInfo,
			&fileIdInformation,
			sizeof(fileIdInformation)))
	{
		state->openedIdentity.deviceOrVolume =
			fileIdInformation.VolumeSerialNumber;
		std::memcpy(
			&state->openedIdentity.nodeLow,
			fileIdInformation.FileId.Identifier,
			sizeof(state->openedIdentity.nodeLow));
		std::memcpy(
			&state->openedIdentity.nodeHigh,
			fileIdInformation.FileId.Identifier +
				sizeof(state->openedIdentity.nodeLow),
			sizeof(state->openedIdentity.nodeHigh));
	}
	else
	{
		state->openedIdentity.deviceOrVolume =
			information.dwVolumeSerialNumber;
		state->openedIdentity.nodeHigh =
			information.nFileIndexHigh;
		state->openedIdentity.nodeLow =
			information.nFileIndexLow;
	}
	state->openedIdentity.linkCount =
		information.nNumberOfLinks;
	state->openedIdentity.valid = true;
	state->handle = opened.release();
#else
	UniqueFileDescriptor opened;
	result.status =
		openAbsoluteDirectoryNoFollow(root, opened);
	if (result.status !=
		RootedResourceReader::Status::Success)
	{
		return result;
	}
	struct stat information = {};
	if (fstat(opened.get(), &information) != 0 ||
		!S_ISDIR(information.st_mode))
	{
		result.status =
			RootedResourceReader::Status::InvalidRoot;
		return result;
	}
	auto state = std::make_shared<
		RootedResourceReader::RootAnchorState>();
	state->openedPath = normalizeRootDirectoryPath(root);
	state->openedIdentity.deviceOrVolume =
		static_cast<std::uint64_t>(information.st_dev);
	state->openedIdentity.nodeLow =
		static_cast<std::uint64_t>(information.st_ino);
	state->openedIdentity.linkCount =
		static_cast<std::uint64_t>(information.st_nlink);
	state->openedIdentity.valid = true;
	state->descriptor = opened.release();
#endif
	result.state = std::move(state);
	result.status = RootedResourceReader::Status::Success;
	return result;
}
}

namespace RootedResourceReader
{
bool RootAnchor::valid() const noexcept
{
	return state != nullptr && state->openedIdentity.valid;
}

const std::filesystem::path& RootAnchor::path() const noexcept
{
	static const std::filesystem::path EmptyPath;
	return state != nullptr ? state->openedPath : EmptyPath;
}

EditorRun::DirectoryIdentity RootAnchor::identity() const noexcept
{
	return state != nullptr
		? state->openedIdentity
		: EditorRun::DirectoryIdentity{};
}

RootAnchorResult openRootAnchor(
	const std::filesystem::path& root)
{
	RootAnchorResult result;
	try
	{
		OpenedRootAnchorState opened =
			openRootAnchorState(root);
		result.status = opened.status;
		result.anchor.state = std::move(opened.state);
		return result;
	}
	catch (...)
	{
		result = {};
		result.status = Status::InvalidRoot;
		return result;
	}
}

#if defined(JXQY_ENABLE_TEST_HOOKS)
void setReadTestHookForTests(const ReadTestHook& hook)
{
	std::lock_guard<std::mutex> lock(
		g_readTestHookMutex);
	g_readTestHook = hook;
}
#endif

ProbeResult probeRegularFileFromRoot(const std::filesystem::path& root,
	std::string_view relativePathUtf8)
{
	try
	{
		ProbeResult result;
		const ResourcePathSafety::StrictRelativePathResult relativePath =
			ResourcePathSafety::normalizeLowercaseStrictRelativeResourcePath(
				relativePathUtf8);
		if (!relativePath.succeeded())
		{
			result.status = Status::UnsafeRelativePath;
			return result;
		}
		return probeRegularFileAtCurrentPath(
			root, relativePath);
	}
	catch (...)
	{
		ProbeResult result;
		result.status = Status::ReadFailed;
		return result;
	}
}

ProbeResult probeRegularFileFromRoot(
	const RootAnchor& root,
	std::string_view relativePathUtf8)
{
	try
	{
		ProbeResult result;
		if (!root.valid())
		{
			result.status = Status::InvalidRoot;
			return result;
		}
		const ResourcePathSafety::StrictRelativePathResult relativePath =
			ResourcePathSafety::normalizeLowercaseStrictRelativeResourcePath(
				relativePathUtf8);
		if (!relativePath.succeeded())
		{
			result.status = Status::UnsafeRelativePath;
			return result;
		}
#ifdef _WIN32
		return probeRegularFileFromHandle(
			root.state->handle, relativePath);
#else
		return probeRegularFileFromDescriptor(
			root.state->descriptor, relativePath);
#endif
	}
	catch (...)
	{
		ProbeResult result;
		result.status = Status::ReadFailed;
		return result;
	}
}

Result readBoundedFileFromRoot(const std::filesystem::path& root,
	std::string_view relativePathUtf8, std::size_t maximumBytes)
{
	try
	{
		Result result;
		const ResourcePathSafety::StrictRelativePathResult relativePath =
			ResourcePathSafety::normalizeLowercaseStrictRelativeResourcePath(
				relativePathUtf8);
		if (!relativePath.succeeded())
		{
			result.status = Status::UnsafeRelativePath;
			return result;
		}
		return readBoundedFileAtCurrentPath(
			root, relativePath, maximumBytes);
	}
	catch (...)
	{
		Result result;
		result.status = Status::ReadFailed;
		return result;
	}
}

Result readBoundedFileFromRoot(
	const RootAnchor& root,
	std::string_view relativePathUtf8,
	std::size_t maximumBytes)
{
	try
	{
		Result result;
		if (!root.valid())
		{
			result.status = Status::InvalidRoot;
			return result;
		}
		const ResourcePathSafety::StrictRelativePathResult relativePath =
			ResourcePathSafety::normalizeLowercaseStrictRelativeResourcePath(
				relativePathUtf8);
		if (!relativePath.succeeded())
		{
			result.status = Status::UnsafeRelativePath;
			return result;
		}
#ifdef _WIN32
		return readBoundedFileFromHandle(
			root.state->handle,
			relativePath,
			maximumBytes);
#else
		return readBoundedFileFromDescriptor(
			root.state->descriptor,
			relativePath,
			maximumBytes);
#endif
	}
	catch (...)
	{
		Result result;
		result.status = Status::ReadFailed;
		return result;
	}
}
}
