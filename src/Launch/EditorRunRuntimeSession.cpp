#include "EditorRunRuntimeSession.h"
#include "../File/StrictRelativeResourcePath.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
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

namespace
{
namespace fs = std::filesystem;

constexpr std::string_view ResourceRoutingContractFileName =
	"resource-routing-contract.json";
constexpr std::string_view RuntimeTraceFileName =
	"runtime-trace.jsonl";
constexpr std::size_t MaximumResourceRoutingContractBytes =
	1024ULL * 1024ULL;
constexpr std::size_t MaximumRoutingContractStringBytes =
	64ULL * 1024ULL;
constexpr std::size_t MaximumRoutingContractDepth = 16;
constexpr std::size_t MaximumTraceContentRootCount = 256;
constexpr std::size_t MaximumTraceOverlayOriginCount =
	4096;
constexpr std::uint64_t MaximumExactJsonInteger =
	9'007'199'254'740'991ULL;

class NativeHandle
{
public:
#if defined(_WIN32)
	using Value = HANDLE;

	static Value invalid()
	{
		return INVALID_HANDLE_VALUE;
	}
#else
	using Value = int;

	static constexpr Value invalid()
	{
		return -1;
	}
#endif

	NativeHandle() = default;

	explicit NativeHandle(Value value) :
		handle(value)
	{
	}

	~NativeHandle()
	{
		reset();
	}

	NativeHandle(const NativeHandle&) = delete;
	NativeHandle& operator=(const NativeHandle&) = delete;

	NativeHandle(NativeHandle&& other) noexcept :
		handle(other.release())
	{
	}

	NativeHandle& operator=(NativeHandle&& other) noexcept
	{
		if (this != &other)
		{
			reset(other.release());
		}
		return *this;
	}

	bool valid() const
	{
		return handle != invalid();
	}

	Value get() const
	{
		return handle;
	}

	Value release()
	{
		const Value result = handle;
		handle = invalid();
		return result;
	}

	void reset(Value value = invalid())
	{
		if (valid())
		{
#if defined(_WIN32)
			CloseHandle(handle);
#else
			close(handle);
#endif
		}
		handle = value;
	}

private:
	Value handle = invalid();
};

enum class NativeNodeKind
{
	Directory,
	RegularFile,
	Other
};

struct NativeMetadata
{
	NativeNodeKind kind = NativeNodeKind::Other;
	std::uint64_t device = 0;
	std::uint64_t nodeHigh = 0;
	std::uint64_t nodeLow = 0;
	std::uint64_t linkCount = 0;
	std::uint64_t size = 0;
	std::uint64_t writeGeneration = 0;
	bool reparsePoint = false;
};

bool nativeMetadata(
	NativeHandle::Value handle,
	NativeMetadata& metadata)
{
	metadata = {};
#if defined(_WIN32)
	BY_HANDLE_FILE_INFORMATION information = {};
	if (handle == INVALID_HANDLE_VALUE ||
		!GetFileInformationByHandle(handle, &information))
	{
		return false;
	}
	metadata.kind =
		(information.dwFileAttributes &
			FILE_ATTRIBUTE_DIRECTORY) != 0
		? NativeNodeKind::Directory
		: NativeNodeKind::RegularFile;
	FILE_ID_INFO fileIdInformation = {};
	if (GetFileInformationByHandleEx(
			handle,
			FileIdInfo,
			&fileIdInformation,
			sizeof(fileIdInformation)))
	{
		metadata.device =
			fileIdInformation.VolumeSerialNumber;
		std::memcpy(
			&metadata.nodeLow,
			fileIdInformation.FileId.Identifier,
			sizeof(metadata.nodeLow));
		std::memcpy(
			&metadata.nodeHigh,
			fileIdInformation.FileId.Identifier +
				sizeof(metadata.nodeLow),
			sizeof(metadata.nodeHigh));
	}
	else
	{
		metadata.device =
			information.dwVolumeSerialNumber;
		metadata.nodeHigh =
			information.nFileIndexHigh;
		metadata.nodeLow =
			information.nFileIndexLow;
	}
	metadata.linkCount = information.nNumberOfLinks;
	metadata.size =
		(static_cast<std::uint64_t>(
			information.nFileSizeHigh) << 32U) |
		information.nFileSizeLow;
	metadata.writeGeneration =
		(static_cast<std::uint64_t>(
			information.ftLastWriteTime.dwHighDateTime) << 32U) |
		information.ftLastWriteTime.dwLowDateTime;
	metadata.reparsePoint =
		(information.dwFileAttributes &
			FILE_ATTRIBUTE_REPARSE_POINT) != 0;
	return true;
#else
	struct stat information = {};
	if (handle < 0 || fstat(handle, &information) != 0)
	{
		return false;
	}
	if (S_ISDIR(information.st_mode))
	{
		metadata.kind = NativeNodeKind::Directory;
	}
	else if (S_ISREG(information.st_mode))
	{
		metadata.kind = NativeNodeKind::RegularFile;
	}
	else
	{
		metadata.kind = NativeNodeKind::Other;
	}
	metadata.device =
		static_cast<std::uint64_t>(information.st_dev);
	metadata.nodeLow =
		static_cast<std::uint64_t>(information.st_ino);
	metadata.linkCount =
		static_cast<std::uint64_t>(information.st_nlink);
	metadata.size =
		information.st_size >= 0
		? static_cast<std::uint64_t>(
			information.st_size)
		: 0;
#if defined(__APPLE__)
	const std::uint64_t seconds =
		static_cast<std::uint64_t>(
			information.st_mtimespec.tv_sec);
	const std::uint64_t nanoseconds =
		static_cast<std::uint64_t>(
			information.st_mtimespec.tv_nsec);
#else
	const std::uint64_t seconds =
		static_cast<std::uint64_t>(
			information.st_mtim.tv_sec);
	const std::uint64_t nanoseconds =
		static_cast<std::uint64_t>(
			information.st_mtim.tv_nsec);
#endif
	metadata.writeGeneration =
		(seconds << 32U) ^ nanoseconds;
	return true;
#endif
}

bool sameNativeIdentity(
	const NativeMetadata& first,
	const NativeMetadata& second)
{
	return first.kind == second.kind &&
		first.device == second.device &&
		first.nodeHigh == second.nodeHigh &&
		first.nodeLow == second.nodeLow;
}

EditorRun::DirectoryIdentity directoryIdentity(
	const NativeMetadata& metadata)
{
	EditorRun::DirectoryIdentity identity;
	if (metadata.kind != NativeNodeKind::Directory ||
		metadata.reparsePoint ||
		metadata.linkCount == 0)
	{
		return identity;
	}
	identity.deviceOrVolume = metadata.device;
	identity.nodeHigh = metadata.nodeHigh;
	identity.nodeLow = metadata.nodeLow;
	identity.linkCount = metadata.linkCount;
	identity.valid = true;
	return identity;
}

bool sameNativeGeneration(
	const NativeMetadata& first,
	const NativeMetadata& second)
{
	return sameNativeIdentity(first, second) &&
		first.linkCount == second.linkCount &&
		first.size == second.size &&
		first.writeGeneration ==
			second.writeGeneration &&
		first.reparsePoint == second.reparsePoint;
}

#if defined(_WIN32)
using NtCreateFileFunction = decltype(&NtCreateFile);

NtCreateFileFunction nativeNtCreateFile()
{
	static const NtCreateFileFunction function = []()
	{
		const HMODULE module =
			GetModuleHandleW(L"ntdll.dll");
		return module != nullptr
			? reinterpret_cast<NtCreateFileFunction>(
				GetProcAddress(module, "NtCreateFile"))
			: nullptr;
	}();
	return function;
}

using GetFileInformationByHandleExFunction =
	BOOL (WINAPI*)(HANDLE, int, void*, DWORD);

GetFileInformationByHandleExFunction
	nativeGetFileInformationByHandleEx()
{
	static const GetFileInformationByHandleExFunction
		function = []()
	{
		const HMODULE module =
			GetModuleHandleW(L"kernel32.dll");
		return module != nullptr
			? reinterpret_cast<
				GetFileInformationByHandleExFunction>(
					GetProcAddress(
						module,
						"GetFileInformationByHandleEx"))
			: nullptr;
	}();
	return function;
}

bool directoryCaseSensitivity(
	HANDLE directory,
	bool& caseSensitive)
{
	// FileCaseSensitiveInfo is 23 on supported Windows SDKs. Use the numeric
	// ABI so this source also compiles with MinGW headers that omit the enum.
	constexpr int FileCaseSensitiveInformationClass = 23;
	constexpr ULONG CaseSensitiveDirectoryFlag = 0x00000001UL;
	struct CaseSensitiveInformation
	{
		ULONG flags = 0;
	};
	const GetFileInformationByHandleExFunction function =
		nativeGetFileInformationByHandleEx();
	CaseSensitiveInformation information;
	if (function == nullptr ||
		!function(
			directory,
			FileCaseSensitiveInformationClass,
			&information,
			sizeof(information)))
	{
		return false;
	}
	caseSensitive =
		(information.flags &
			CaseSensitiveDirectoryFlag) != 0;
	return true;
}

bool handleHasExactLeafName(
	HANDLE handle,
	std::string_view expectedName)
{
	if (handle == INVALID_HANDLE_VALUE ||
		expectedName.empty())
	{
		return false;
	}
	const std::wstring expected =
		fs::u8path(expectedName).wstring();
	const DWORD flags =
		FILE_NAME_NORMALIZED |
		VOLUME_NAME_DOS;
	const DWORD required =
		GetFinalPathNameByHandleW(
			handle, nullptr, 0, flags);
	if (required == 0)
	{
		return false;
	}
	std::vector<wchar_t> buffer(
		static_cast<std::size_t>(required) + 1);
	const DWORD written =
		GetFinalPathNameByHandleW(
			handle,
			buffer.data(),
			static_cast<DWORD>(buffer.size()),
			flags);
	if (written == 0 ||
		written >= buffer.size())
	{
		return false;
	}
	const fs::path finalPath(
		std::wstring(
			buffer.data(),
			static_cast<std::size_t>(written)));
	return finalPath.filename().wstring() ==
		expected;
}

enum class RelativeOpenStatus
{
	Success,
	NotFound,
	Failed
};

bool ntStatusIsNotFound(NTSTATUS status)
{
	const ULONG value = static_cast<ULONG>(status);
	return value == 0xC000000FUL ||
		value == 0xC0000034UL ||
		value == 0xC000003AUL;
}

RelativeOpenStatus openRelativeWindows(
	HANDLE parent,
	std::string_view name,
	bool directory,
	bool anyNode,
	bool caseSensitive,
	NativeHandle& opened)
{
	opened.reset();
	const NtCreateFileFunction function =
		nativeNtCreateFile();
	if (function == nullptr ||
		parent == INVALID_HANDLE_VALUE ||
		name.empty())
	{
		return RelativeOpenStatus::Failed;
	}
	const std::wstring wideName =
		fs::u8path(name).wstring();
	if (wideName.empty() ||
		wideName.size() >
			static_cast<std::size_t>(
				(std::numeric_limits<USHORT>::max)()) /
				sizeof(wchar_t))
	{
		return RelativeOpenStatus::Failed;
	}
	UNICODE_STRING leaf = {};
	leaf.Buffer = const_cast<PWSTR>(wideName.data());
	leaf.Length = static_cast<USHORT>(
		wideName.size() * sizeof(wchar_t));
	leaf.MaximumLength = leaf.Length;
	OBJECT_ATTRIBUTES attributes = {};
	InitializeObjectAttributes(
		&attributes,
		&leaf,
		caseSensitive ? 0 : OBJ_CASE_INSENSITIVE,
		parent,
		nullptr);
	IO_STATUS_BLOCK ioStatus = {};
	HANDLE value = INVALID_HANDLE_VALUE;
	ULONG options =
		FILE_OPEN_REPARSE_POINT |
		FILE_SYNCHRONOUS_IO_NONALERT;
	ACCESS_MASK access =
		FILE_READ_ATTRIBUTES | SYNCHRONIZE;
	if (!anyNode)
	{
		if (directory)
		{
			options |= FILE_DIRECTORY_FILE;
			access |= FILE_LIST_DIRECTORY;
		}
		else
		{
			options |= FILE_NON_DIRECTORY_FILE;
			access |= FILE_READ_DATA;
		}
	}
	const NTSTATUS status = function(
		&value,
		access,
		&attributes,
		&ioStatus,
		nullptr,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ |
			FILE_SHARE_WRITE |
			FILE_SHARE_DELETE,
		FILE_OPEN,
		options,
		nullptr,
		0);
	if (status < 0 ||
		value == INVALID_HANDLE_VALUE)
	{
		if (value != INVALID_HANDLE_VALUE)
		{
			CloseHandle(value);
		}
		return ntStatusIsNotFound(status)
			? RelativeOpenStatus::NotFound
			: RelativeOpenStatus::Failed;
	}
	opened.reset(value);
	return RelativeOpenStatus::Success;
}
#endif

enum class NodeOpenStatus
{
	Success,
	NotFound,
	Unsafe,
	Failed
};

bool openAbsoluteDirectoryNoFollow(
	const fs::path& path,
	NativeHandle& opened,
	NativeMetadata& metadata,
	bool& caseSensitive)
{
	opened.reset();
#if defined(_WIN32)
	const HANDLE value = CreateFileW(
		path.c_str(),
		FILE_LIST_DIRECTORY |
			FILE_READ_ATTRIBUTES |
			SYNCHRONIZE,
		FILE_SHARE_READ |
			FILE_SHARE_WRITE |
			FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS |
			FILE_FLAG_OPEN_REPARSE_POINT,
		nullptr);
	if (value == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	opened.reset(value);
	if (!nativeMetadata(value, metadata) ||
		metadata.kind != NativeNodeKind::Directory ||
		metadata.reparsePoint ||
		!directoryCaseSensitivity(
			value, caseSensitive))
	{
		opened.reset();
		return false;
	}
#else
	int flags = O_RDONLY | O_DIRECTORY;
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	flags |= O_NOFOLLOW;
#endif
	const int value = open(path.c_str(), flags);
	if (value < 0)
	{
		return false;
	}
	opened.reset(value);
	if (!nativeMetadata(value, metadata) ||
		metadata.kind != NativeNodeKind::Directory)
	{
		opened.reset();
		return false;
	}
	caseSensitive = true;
#endif
	return true;
}

NodeOpenStatus openChildDirectoryNoFollow(
	const NativeHandle& parent,
	bool parentCaseSensitive,
	std::string_view name,
	NativeHandle& opened,
	NativeMetadata& metadata,
	bool& caseSensitive)
{
	opened.reset();
#if defined(_WIN32)
	const RelativeOpenStatus status =
		openRelativeWindows(
			parent.get(), name, true, false,
			parentCaseSensitive, opened);
	if (status != RelativeOpenStatus::Success)
	{
		return status == RelativeOpenStatus::NotFound
			? NodeOpenStatus::NotFound
			: NodeOpenStatus::Failed;
	}
	if (!nativeMetadata(opened.get(), metadata) ||
		metadata.kind != NativeNodeKind::Directory ||
		metadata.reparsePoint ||
		!handleHasExactLeafName(
			opened.get(), name) ||
		!directoryCaseSensitivity(
			opened.get(), caseSensitive))
	{
		opened.reset();
		return NodeOpenStatus::Unsafe;
	}
#else
	int flags = O_RDONLY | O_DIRECTORY;
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	flags |= O_NOFOLLOW;
#endif
	const std::string leaf(name);
	const int value =
		openat(parent.get(), leaf.c_str(), flags);
	if (value < 0)
	{
		return errno == ENOENT
			? NodeOpenStatus::NotFound
			: NodeOpenStatus::Failed;
	}
	opened.reset(value);
	if (!nativeMetadata(value, metadata) ||
		metadata.kind != NativeNodeKind::Directory)
	{
		opened.reset();
		return NodeOpenStatus::Unsafe;
	}
	caseSensitive = true;
	(void)parentCaseSensitive;
#endif
	return NodeOpenStatus::Success;
}

NodeOpenStatus openChildFileNoFollow(
	const NativeHandle& parent,
	bool parentCaseSensitive,
	std::string_view name,
	NativeHandle& opened,
	NativeMetadata& metadata)
{
	opened.reset();
#if defined(_WIN32)
	const RelativeOpenStatus status =
		openRelativeWindows(
			parent.get(), name, false, false,
			parentCaseSensitive, opened);
	if (status != RelativeOpenStatus::Success)
	{
		return status == RelativeOpenStatus::NotFound
			? NodeOpenStatus::NotFound
			: NodeOpenStatus::Failed;
	}
	if (!handleHasExactLeafName(
			opened.get(), name))
	{
		opened.reset();
		return NodeOpenStatus::Unsafe;
	}
#else
	int flags = O_RDONLY;
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	flags |= O_NOFOLLOW;
#endif
	const std::string leaf(name);
	const int value =
		openat(parent.get(), leaf.c_str(), flags);
	if (value < 0)
	{
		return errno == ENOENT
			? NodeOpenStatus::NotFound
			: NodeOpenStatus::Failed;
	}
	opened.reset(value);
	(void)parentCaseSensitive;
#endif
	if (!nativeMetadata(opened.get(), metadata) ||
		metadata.kind != NativeNodeKind::RegularFile ||
		metadata.reparsePoint ||
		metadata.linkCount != 1)
	{
		opened.reset();
		return NodeOpenStatus::Unsafe;
	}
	return NodeOpenStatus::Success;
}

bool seekFileStart(NativeHandle::Value handle)
{
#if defined(_WIN32)
	LARGE_INTEGER offset = {};
	return SetFilePointerEx(
		handle, offset, nullptr, FILE_BEGIN) != 0;
#else
	return handle >= 0;
#endif
}

bool readFileChunk(
	NativeHandle::Value handle,
	std::uint64_t offset,
	unsigned char* output,
	std::size_t requested,
	std::size_t& readBytes)
{
	readBytes = 0;
#if defined(_WIN32)
	if (requested >
		static_cast<std::size_t>(
			(std::numeric_limits<DWORD>::max)()))
	{
		return false;
	}
	DWORD current = 0;
	if (!ReadFile(
			handle,
			output,
			static_cast<DWORD>(requested),
			&current,
			nullptr))
	{
		return false;
	}
	readBytes = current;
	(void)offset;
	return true;
#else
	ssize_t current = -1;
	do
	{
		current = pread(
			handle,
			output,
			requested,
			static_cast<off_t>(offset));
	}
	while (current < 0 && errno == EINTR);
	if (current < 0)
	{
		return false;
	}
	readBytes = static_cast<std::size_t>(current);
	return true;
#endif
}

enum class HeldReadStatus
{
	Success,
	TooLarge,
	Unsafe,
	ReadFailed,
	Changed
};

HeldReadStatus readHeldFileBounded(
	const NativeHandle& file,
	const NativeMetadata& openedMetadata,
	std::size_t maximumBytes,
	std::string& bytes)
{
	bytes.clear();
	NativeMetadata before;
	if (!nativeMetadata(file.get(), before) ||
		!sameNativeGeneration(
			openedMetadata, before) ||
		before.kind != NativeNodeKind::RegularFile ||
		before.reparsePoint ||
		before.linkCount != 1)
	{
		return HeldReadStatus::Unsafe;
	}
	if (before.size > maximumBytes ||
		before.size >
			static_cast<std::uint64_t>(
				(std::numeric_limits<std::size_t>::max)()))
	{
		return HeldReadStatus::TooLarge;
	}
	bytes.resize(
		static_cast<std::size_t>(before.size));
	if (!seekFileStart(file.get()))
	{
		bytes.clear();
		return HeldReadStatus::ReadFailed;
	}
	std::size_t total = 0;
	while (total < bytes.size())
	{
		const std::size_t requested =
			(std::min)(
				bytes.size() - total,
				static_cast<std::size_t>(
					1024 * 1024));
		std::size_t current = 0;
		if (!readFileChunk(
				file.get(),
				total,
				reinterpret_cast<unsigned char*>(
					bytes.data() + total),
				requested,
				current) ||
			current == 0)
		{
			bytes.clear();
			return HeldReadStatus::ReadFailed;
		}
		total += current;
	}
	NativeMetadata after;
	if (!nativeMetadata(file.get(), after) ||
		!sameNativeGeneration(before, after))
	{
		bytes.clear();
		return HeldReadStatus::Changed;
	}
	return HeldReadStatus::Success;
}

std::size_t validUtf8SequenceLength(
	std::string_view value,
	std::size_t offset)
{
	const unsigned char first =
		static_cast<unsigned char>(value[offset]);
	if (first < 0x80U)
	{
		return 1;
	}
	std::size_t length = 0;
	std::uint32_t codePoint = 0;
	std::uint32_t minimum = 0;
	if ((first & 0xE0U) == 0xC0U)
	{
		length = 2;
		codePoint = first & 0x1FU;
		minimum = 0x80U;
	}
	else if ((first & 0xF0U) == 0xE0U)
	{
		length = 3;
		codePoint = first & 0x0FU;
		minimum = 0x800U;
	}
	else if ((first & 0xF8U) == 0xF0U)
	{
		length = 4;
		codePoint = first & 0x07U;
		minimum = 0x10000U;
	}
	else
	{
		return 0;
	}
	if (offset + length > value.size())
	{
		return 0;
	}
	for (std::size_t index = 1;
		index < length; ++index)
	{
		const unsigned char continuation =
			static_cast<unsigned char>(
				value[offset + index]);
		if ((continuation & 0xC0U) != 0x80U)
		{
			return 0;
		}
		codePoint =
			(codePoint << 6U) |
			(continuation & 0x3FU);
	}
	return codePoint >= minimum &&
		codePoint <= 0x10FFFFU &&
		!(codePoint >= 0xD800U &&
			codePoint <= 0xDFFFU)
		? length
		: 0;
}

bool appendUtf8CodePoint(
	std::uint32_t value,
	std::string& output,
	std::size_t maximumBytes)
{
	std::array<char, 4> bytes = {};
	std::size_t count = 0;
	if (value <= 0x7FU)
	{
		bytes[0] = static_cast<char>(value);
		count = 1;
	}
	else if (value <= 0x7FFU)
	{
		bytes[0] = static_cast<char>(
			0xC0U | (value >> 6U));
		bytes[1] = static_cast<char>(
			0x80U | (value & 0x3FU));
		count = 2;
	}
	else if (value <= 0xFFFFU)
	{
		bytes[0] = static_cast<char>(
			0xE0U | (value >> 12U));
		bytes[1] = static_cast<char>(
			0x80U | ((value >> 6U) & 0x3FU));
		bytes[2] = static_cast<char>(
			0x80U | (value & 0x3FU));
		count = 3;
	}
	else
	{
		bytes[0] = static_cast<char>(
			0xF0U | (value >> 18U));
		bytes[1] = static_cast<char>(
			0x80U | ((value >> 12U) & 0x3FU));
		bytes[2] = static_cast<char>(
			0x80U | ((value >> 6U) & 0x3FU));
		bytes[3] = static_cast<char>(
			0x80U | (value & 0x3FU));
		count = 4;
	}
	if (output.size() >
		maximumBytes - (std::min)(
			maximumBytes, count))
	{
		return false;
	}
	output.append(bytes.data(), count);
	return true;
}

class ResourceRoutingContractParser
{
public:
	explicit ResourceRoutingContractParser(std::string_view value) :
		bytes(value)
	{
	}

	bool parse()
	{
		traceOverlayOrigins.clear();
		previousOverlayVirtualPath.clear();
		position = 0;
		skipWhitespace();
		if (!consume('{'))
		{
			return false;
		}
		bool hasSchema = false;
		bool hasRoots = false;
		bool hasTraceOverlayOrigins = false;
		skipWhitespace();
		if (consume('}'))
		{
			return false;
		}
		while (true)
		{
			std::string key;
			if (!parseString(key, 128) ||
				!consumeAfterWhitespace(':'))
			{
				return false;
			}
			if (key == "schemaVersion")
			{
				if (hasSchema ||
					!parseExactNumber("1"))
				{
					return false;
				}
				hasSchema = true;
			}
			else if (key == "roots")
			{
				if (hasRoots || !skipValue(1))
				{
					return false;
				}
				hasRoots = true;
			}
			else if (key ==
				"traceOverlayOrigins")
			{
				if (hasTraceOverlayOrigins ||
					!parseTraceOverlayOrigins())
				{
					return false;
				}
				hasTraceOverlayOrigins = true;
			}
			else if (!skipValue(1))
			{
				return false;
			}
			skipWhitespace();
			if (consume('}'))
			{
				break;
			}
			if (!consume(','))
			{
				return false;
			}
		}
		skipWhitespace();
		return hasSchema &&
			position == bytes.size();
	}

	std::vector<EditorRun::RuntimeSessionOverlayOrigin>
	takeTraceOverlayOrigins()
	{
		return std::move(traceOverlayOrigins);
	}

private:
	bool parseTraceOverlayOrigins()
	{
		skipWhitespace();
		if (!consume('['))
		{
			return false;
		}
		skipWhitespace();
		if (consume(']'))
		{
			return true;
		}
		while (true)
		{
			if (traceOverlayOrigins.size() >=
					MaximumTraceOverlayOriginCount ||
				!parseTraceOverlayOrigin())
			{
				return false;
			}
			skipWhitespace();
			if (consume(']'))
			{
				return true;
			}
			if (!consume(','))
			{
				return false;
			}
		}
	}

	bool parseTraceOverlayOrigin()
	{
		skipWhitespace();
		if (!consume('{'))
		{
			return false;
		}
		bool hasVirtualPath = false;
		bool hasRootOrdinal = false;
		bool hasRootKind = false;
		bool hasResourcePackId = false;
		bool hasRootPath = false;
		std::string virtualPath;
		std::uint64_t rootOrdinal = 0;
		std::string rootKind;
		std::string resourcePackId;
		std::string rootPath;
		skipWhitespace();
		if (consume('}'))
		{
			return false;
		}
		while (true)
		{
			std::string key;
			if (!parseString(key, 128) ||
				!consumeAfterWhitespace(':'))
			{
				return false;
			}
			if (key == "virtualPath")
			{
				if (hasVirtualPath ||
					!parseString(
						virtualPath,
						MaximumRoutingContractStringBytes))
				{
					return false;
				}
				hasVirtualPath = true;
			}
			else if (key == "rootOrdinal")
			{
				if (hasRootOrdinal ||
					!parseUnsignedExactJsonInteger(
						rootOrdinal))
				{
					return false;
				}
				hasRootOrdinal = true;
			}
			else if (key == "rootKind")
			{
				if (hasRootKind ||
					!parseString(rootKind, 32))
				{
					return false;
				}
				hasRootKind = true;
			}
			else if (key == "resourcePackId")
			{
				if (hasResourcePackId ||
					!parseString(
						resourcePackId,
						MaximumRoutingContractStringBytes))
				{
					return false;
				}
				hasResourcePackId = true;
			}
			else if (key == "rootPath")
			{
				if (hasRootPath ||
					!parseString(
						rootPath,
						MaximumRoutingContractStringBytes))
				{
					return false;
				}
				hasRootPath = true;
			}
			else if (!skipValue(2))
			{
				return false;
			}
			skipWhitespace();
			if (consume('}'))
			{
				break;
			}
			if (!consume(','))
			{
				return false;
			}
		}
		const ResourcePathSafety::StrictRelativePathResult
			normalized =
				ResourcePathSafety::
					normalizeStrictRelativeResourcePath(
						virtualPath);
		if (!hasVirtualPath || !hasRootOrdinal ||
			!normalized.succeeded() ||
			normalized.normalizedPath != virtualPath ||
			rootOrdinal >= MaximumTraceContentRootCount ||
			(!previousOverlayVirtualPath.empty() &&
				previousOverlayVirtualPath >=
					virtualPath))
		{
			return false;
		}
		EditorRun::RuntimeSessionOverlayOrigin origin;
		origin.virtualPath = virtualPath;
		origin.rootOrdinal = rootOrdinal;
		if (hasRootKind)
		{
			if (rootKind == "active")
			{
				origin.rootKind =
					EditorRun::RuntimeTraceRootKind::Active;
			}
			else if (rootKind == "dependency-id")
			{
				origin.rootKind =
					EditorRun::RuntimeTraceRootKind::
						DependencyId;
			}
			else if (rootKind == "common")
			{
				origin.rootKind =
					EditorRun::RuntimeTraceRootKind::Common;
			}
		}
		if (hasResourcePackId &&
			!resourcePackId.empty())
		{
			origin.resourcePackId =
				std::move(resourcePackId);
		}
		if (hasRootPath)
		{
			try
			{
				const fs::path parsedRoot =
					fs::u8path(rootPath).
						lexically_normal();
				if (!parsedRoot.empty() &&
					parsedRoot.is_absolute())
				{
					origin.rootPath =
						std::move(parsedRoot);
				}
			}
			catch (...)
			{
				origin.rootPath.clear();
			}
		}
		traceOverlayOrigins.push_back(
			std::move(origin));
		previousOverlayVirtualPath =
			std::move(virtualPath);
		return true;
	}

	bool parseUnsignedExactJsonInteger(
		std::uint64_t& value)
	{
		skipWhitespace();
		const std::size_t start = position;
		if (!skipNumber())
		{
			return false;
		}
		const std::string_view text =
			bytes.substr(start, position - start);
		if (text.empty() ||
			(text.size() > 1 &&
				text.front() == '0'))
		{
			return false;
		}
		std::uint64_t parsed = 0;
		for (const char character : text)
		{
			if (character < '0' ||
				character > '9')
			{
				return false;
			}
			const std::uint64_t digit =
				static_cast<std::uint64_t>(
					character - '0');
			if (parsed >
				(MaximumExactJsonInteger -
					digit) / 10U)
			{
				return false;
			}
			parsed = parsed * 10U + digit;
		}
		value = parsed;
		return true;
	}

	bool parseString(
		std::string& output,
		std::size_t maximumBytes)
	{
		output.clear();
		skipWhitespace();
		if (!consume('"'))
		{
			return false;
		}
		while (position < bytes.size())
		{
			const unsigned char current =
				static_cast<unsigned char>(
					bytes[position++]);
			if (current == '"')
			{
				return true;
			}
			if (current < 0x20U)
			{
				return false;
			}
			if (current == '\\')
			{
				if (position >= bytes.size())
				{
					return false;
				}
				const char escaped =
					bytes[position++];
				switch (escaped)
				{
				case '"':
				case '\\':
				case '/':
					if (output.size() >= maximumBytes)
					{
						return false;
					}
					output.push_back(escaped);
					break;
				case 'b':
				case 'f':
				case 'n':
				case 'r':
				case 't':
				{
					if (output.size() >= maximumBytes)
					{
						return false;
					}
					const char decoded =
						escaped == 'b' ? '\b' :
						escaped == 'f' ? '\f' :
						escaped == 'n' ? '\n' :
						escaped == 'r' ? '\r' : '\t';
					output.push_back(decoded);
					break;
				}
				case 'u':
					if (!parseUnicodeEscape(
							output,
							maximumBytes))
					{
						return false;
					}
					break;
				default:
					return false;
				}
				continue;
			}
			if (current < 0x80U)
			{
				if (output.size() >= maximumBytes)
				{
					return false;
				}
				output.push_back(
					static_cast<char>(current));
				continue;
			}
			const std::size_t sequenceStart =
				position - 1;
			const std::size_t length =
				validUtf8SequenceLength(
					bytes, sequenceStart);
			if (length == 0 ||
				output.size() >
					maximumBytes - (std::min)(
						maximumBytes, length))
			{
				return false;
			}
			output.append(
				bytes.substr(sequenceStart, length));
			position = sequenceStart + length;
		}
		return false;
	}

	bool parseUnicodeEscape(
		std::string& output,
		std::size_t maximumBytes)
	{
		std::uint32_t first = 0;
		if (!parseHexQuad(first))
		{
			return false;
		}
		std::uint32_t codePoint = first;
		if (first >= 0xD800U &&
			first <= 0xDBFFU)
		{
			if (position + 2 > bytes.size() ||
				bytes[position] != '\\' ||
				bytes[position + 1] != 'u')
			{
				return false;
			}
			position += 2;
			std::uint32_t second = 0;
			if (!parseHexQuad(second) ||
				second < 0xDC00U ||
				second > 0xDFFFU)
			{
				return false;
			}
			codePoint =
				0x10000U +
				((first - 0xD800U) << 10U) +
				(second - 0xDC00U);
		}
		else if (first >= 0xDC00U &&
			first <= 0xDFFFU)
		{
			return false;
		}
		return appendUtf8CodePoint(
			codePoint, output, maximumBytes);
	}

	bool parseHexQuad(std::uint32_t& value)
	{
		if (position + 4 > bytes.size())
		{
			return false;
		}
		value = 0;
		for (std::size_t index = 0;
			index < 4; ++index)
		{
			const char character =
				bytes[position++];
			std::uint32_t digit = 0;
			if (character >= '0' &&
				character <= '9')
			{
				digit = character - '0';
			}
			else if (character >= 'a' &&
				character <= 'f')
			{
				digit =
					10U + character - 'a';
			}
			else if (character >= 'A' &&
				character <= 'F')
			{
				digit =
					10U + character - 'A';
			}
			else
			{
				return false;
			}
			value = (value << 4U) | digit;
		}
		return true;
	}

	bool parseExactNumber(
		std::string_view expected)
	{
		skipWhitespace();
		const std::size_t start = position;
		if (!skipNumber())
		{
			return false;
		}
		return bytes.substr(
			start, position - start) == expected;
	}

	bool skipValue(std::size_t depth)
	{
		if (depth > MaximumRoutingContractDepth)
		{
			return false;
		}
		skipWhitespace();
		if (position >= bytes.size())
		{
			return false;
		}
		if (bytes[position] == '"')
		{
			std::string ignored;
			return parseString(
				ignored,
				MaximumRoutingContractStringBytes);
		}
		if (bytes[position] == '{')
		{
			++position;
			skipWhitespace();
			if (consume('}'))
			{
				return true;
			}
			while (true)
			{
				std::string key;
				if (!parseString(key, 128) ||
					!consumeAfterWhitespace(':') ||
					!skipValue(depth + 1))
				{
					return false;
				}
				skipWhitespace();
				if (consume('}'))
				{
					return true;
				}
				if (!consume(','))
				{
					return false;
				}
			}
		}
		if (bytes[position] == '[')
		{
			++position;
			skipWhitespace();
			if (consume(']'))
			{
				return true;
			}
			while (true)
			{
				if (!skipValue(depth + 1))
				{
					return false;
				}
				skipWhitespace();
				if (consume(']'))
				{
					return true;
				}
				if (!consume(','))
				{
					return false;
				}
			}
		}
		if (bytes.substr(position, 4) == "true" ||
			bytes.substr(position, 4) == "null")
		{
			position += 4;
			return true;
		}
		if (bytes.substr(position, 5) == "false")
		{
			position += 5;
			return true;
		}
		return skipNumber();
	}

	bool skipNumber()
	{
		const std::size_t start = position;
		if (position < bytes.size() &&
			bytes[position] == '-')
		{
			++position;
		}
		if (position >= bytes.size())
		{
			return false;
		}
		if (bytes[position] == '0')
		{
			++position;
		}
		else if (bytes[position] >= '1' &&
			bytes[position] <= '9')
		{
			do
			{
				++position;
			}
			while (position < bytes.size() &&
				bytes[position] >= '0' &&
				bytes[position] <= '9');
		}
		else
		{
			return false;
		}
		if (position < bytes.size() &&
			bytes[position] == '.')
		{
			++position;
			const std::size_t fractionStart =
				position;
			while (position < bytes.size() &&
				bytes[position] >= '0' &&
				bytes[position] <= '9')
			{
				++position;
			}
			if (position == fractionStart)
			{
				return false;
			}
		}
		if (position < bytes.size() &&
			(bytes[position] == 'e' ||
				bytes[position] == 'E'))
		{
			++position;
			if (position < bytes.size() &&
				(bytes[position] == '+' ||
					bytes[position] == '-'))
			{
				++position;
			}
			const std::size_t exponentStart =
				position;
			while (position < bytes.size() &&
				bytes[position] >= '0' &&
				bytes[position] <= '9')
			{
				++position;
			}
			if (position == exponentStart)
			{
				return false;
			}
		}
		return position > start;
	}

	bool consumeAfterWhitespace(char value)
	{
		skipWhitespace();
		return consume(value);
	}

	bool consume(char value)
	{
		if (position >= bytes.size() ||
			bytes[position] != value)
		{
			return false;
		}
		++position;
		return true;
	}

	void skipWhitespace()
	{
		while (position < bytes.size() &&
			(bytes[position] == ' ' ||
				bytes[position] == '\t' ||
				bytes[position] == '\r' ||
				bytes[position] == '\n'))
		{
			++position;
		}
	}

	std::string_view bytes;
	std::size_t position = 0;
	std::string previousOverlayVirtualPath;
	std::vector<EditorRun::RuntimeSessionOverlayOrigin>
		traceOverlayOrigins;
};

void fail(
	EditorRun::RuntimeSessionResult& result,
	EditorRun::RuntimeSessionFailureCategory category,
	EditorRun::RuntimeSessionError error,
	std::string diagnosticCode,
	std::string message,
	std::string fieldPath = {},
	fs::path problemPath = {})
{
	result.failureCategory = category;
	result.error = error;
	result.diagnosticCode =
		std::move(diagnosticCode);
	result.message = std::move(message);
	result.fieldPath = std::move(fieldPath);
	result.problemPath = std::move(problemPath);
}

void failDescriptorRead(
	EditorRun::RuntimeSessionResult& result,
	EditorRun::RuntimeSessionError error,
	std::string message,
	const fs::path& path)
{
	fail(
		result,
		EditorRun::RuntimeSessionFailureCategory::
			DescriptorRead,
		error,
		error == EditorRun::RuntimeSessionError::
				DescriptorTooLarge
			? "editor_run.descriptor.too_large"
			: "editor_run.descriptor.read_failed",
		std::move(message),
		{},
		path);
}

std::string descriptorDiagnosticCode(
	EditorRun::DescriptorError error)
{
	switch (error)
	{
	case EditorRun::DescriptorError::UnsupportedVersion:
		return
			"editor_run.descriptor.unsupported_version";
	case EditorRun::DescriptorError::UnsafeVirtualPath:
	case EditorRun::DescriptorError::InvalidHostPath:
		return "editor_run.path.unsafe";
	default:
		return "editor_run.descriptor.invalid";
	}
}

}

namespace EditorRun
{
RuntimeSessionResult loadEditorRunRuntimeSession(
	const fs::path& requestedDescriptorPath)
{
	RuntimeSessionResult result;
	try
	{
		if (requestedDescriptorPath.empty() ||
			!requestedDescriptorPath.is_absolute())
		{
			fail(
				result,
				RuntimeSessionFailureCategory::
					DescriptorValidation,
				RuntimeSessionError::
					DescriptorPathInvalid,
				"editor_run.path.unsafe",
				"Editor-run descriptor path must be absolute",
				"descriptorPath",
				requestedDescriptorPath);
			return result;
		}

		const fs::path descriptorPath =
			requestedDescriptorPath.lexically_normal();
		const fs::path sessionRoot =
			descriptorPath.parent_path().
				lexically_normal();
		const std::string descriptorLeaf =
			descriptorPath.filename().
				generic_u8string();
		if (sessionRoot.empty() ||
			descriptorLeaf.empty())
		{
			failDescriptorRead(
				result,
				RuntimeSessionError::
					DescriptorOpenFailed,
				"Editor-run descriptor has no usable parent or file name",
				descriptorPath);
			return result;
		}

		NativeHandle sessionRootHandle;
		NativeMetadata sessionRootMetadata;
		bool sessionRootCaseSensitive = true;
		if (!openAbsoluteDirectoryNoFollow(
				sessionRoot,
				sessionRootHandle,
				sessionRootMetadata,
				sessionRootCaseSensitive))
		{
			failDescriptorRead(
				result,
				RuntimeSessionError::
					DescriptorOpenFailed,
				"Editor-run session directory cannot be opened",
				sessionRoot);
			return result;
		}

		NativeHandle descriptorHandle;
		NativeMetadata descriptorMetadata;
		const NodeOpenStatus descriptorOpen =
			openChildFileNoFollow(
				sessionRootHandle,
				sessionRootCaseSensitive,
				descriptorLeaf,
				descriptorHandle,
				descriptorMetadata);
		if (descriptorOpen !=
			NodeOpenStatus::Success)
		{
			failDescriptorRead(
				result,
				descriptorOpen == NodeOpenStatus::Unsafe
					? RuntimeSessionError::DescriptorUnsafe
					: RuntimeSessionError::DescriptorOpenFailed,
				"Editor-run descriptor cannot be opened as a regular file",
				descriptorPath);
			return result;
		}

		std::string descriptorBytes;
		const HeldReadStatus descriptorRead =
			readHeldFileBounded(
				descriptorHandle,
				descriptorMetadata,
				MaximumDescriptorBytes,
				descriptorBytes);
		if (descriptorRead != HeldReadStatus::Success)
		{
			failDescriptorRead(
				result,
				descriptorRead == HeldReadStatus::TooLarge
					? RuntimeSessionError::DescriptorTooLarge
					: RuntimeSessionError::DescriptorReadFailed,
				descriptorRead == HeldReadStatus::TooLarge
					? "Editor-run descriptor exceeds the byte limit"
					: "Editor-run descriptor cannot be read",
				descriptorPath);
			return result;
		}

		DescriptorResult descriptorResult =
			parseEditorRunDescriptor(descriptorBytes);
		if (!descriptorResult.succeeded())
		{
			fail(
				result,
				RuntimeSessionFailureCategory::
					DescriptorValidation,
				RuntimeSessionError::
					DescriptorInvalid,
				descriptorDiagnosticCode(
					descriptorResult.error),
				descriptorResult.message,
				descriptorResult.fieldPath,
				descriptorPath);
			result.descriptorError =
				descriptorResult.error;
			result.line = descriptorResult.line;
			result.column = descriptorResult.column;
			return result;
		}
		Descriptor descriptor =
			std::move(descriptorResult.descriptor);

		std::array<fs::path, 4> outputPaths =
		{
			descriptor.overlayRoot.lexically_normal(),
			descriptor.isolatedSaveRoot.lexically_normal(),
			descriptor.applicationStateRoot.lexically_normal(),
			descriptor.diagnosticsPath.parent_path().
				lexically_normal()
		};
		const auto isContainedOutput =
			[&sessionRoot](const fs::path& path)
			{
				if (path.empty() || !path.is_absolute())
				{
					return false;
				}
				const fs::path relative =
					path.lexically_relative(sessionRoot);
				if (relative.empty() ||
					relative == "." ||
					relative.is_absolute())
				{
					return false;
				}
				for (const fs::path& component : relative)
				{
					if (component == "..")
					{
						return false;
					}
				}
				return true;
			};

		std::array<NativeHandle, 4> outputHandles;
		std::array<NativeMetadata, 4> outputMetadata;
		std::array<bool, 4> outputCaseSensitive =
			{ true, true, true, true };
		for (std::size_t index = 0;
			index < outputPaths.size();
			++index)
		{
			if (!isContainedOutput(outputPaths[index]) ||
				!openAbsoluteDirectoryNoFollow(
					outputPaths[index],
					outputHandles[index],
					outputMetadata[index],
					outputCaseSensitive[index]))
			{
				fail(
					result,
					RuntimeSessionFailureCategory::Isolation,
					RuntimeSessionError::
						OutputDirectoryUnavailable,
					"editor_run.isolation.unavailable",
					"Editor-run private output directory must be an existing directory inside the session root",
					"outputPaths",
					outputPaths[index]);
				return result;
			}
		}
		for (std::size_t first = 0;
			first < outputPaths.size();
			++first)
		{
			for (std::size_t second = first + 1;
				second < outputPaths.size();
				++second)
			{
				if (outputPaths[first] ==
						outputPaths[second] ||
					sameNativeIdentity(
						outputMetadata[first],
						outputMetadata[second]))
				{
					fail(
						result,
						RuntimeSessionFailureCategory::Isolation,
						RuntimeSessionError::
							OutputTopologyInvalid,
						"editor_run.isolation.unavailable",
						"Editor-run private output directories must be distinct",
						"outputPaths",
						outputPaths[first]);
					return result;
				}
			}
		}

		const fs::path diagnosticsPath =
			descriptor.diagnosticsPath.
				lexically_normal();
		const fs::path logPath =
			descriptor.logPath.lexically_normal();
		const fs::path runtimeTracePath =
			outputPaths[3] /
			fs::u8path(RuntimeTraceFileName);
		if (!diagnosticsPath.is_absolute() ||
			!logPath.is_absolute() ||
			diagnosticsPath.parent_path() !=
				outputPaths[3] ||
			logPath.parent_path() != outputPaths[3] ||
			diagnosticsPath.filename().empty() ||
			logPath.filename().empty() ||
			diagnosticsPath == logPath ||
			diagnosticsPath == runtimeTracePath ||
			logPath == runtimeTracePath)
		{
			fail(
				result,
				RuntimeSessionFailureCategory::Isolation,
				RuntimeSessionError::
					OutputTopologyInvalid,
				"editor_run.isolation.unavailable",
				"Editor-run diagnostic outputs must be distinct files in their private output directory",
				"diagnosticsPath",
				outputPaths[3]);
			return result;
		}

		NativeHandle resourceRoutingContractHandle;
		NativeMetadata resourceRoutingContractMetadata;
		const NodeOpenStatus contractOpen =
			openChildFileNoFollow(
				sessionRootHandle,
				sessionRootCaseSensitive,
				ResourceRoutingContractFileName,
				resourceRoutingContractHandle,
				resourceRoutingContractMetadata);
		std::string resourceRoutingContractBytes;
		if (contractOpen != NodeOpenStatus::Success ||
			readHeldFileBounded(
				resourceRoutingContractHandle,
				resourceRoutingContractMetadata,
				MaximumResourceRoutingContractBytes,
				resourceRoutingContractBytes) !=
				HeldReadStatus::Success)
		{
			fail(
				result,
				RuntimeSessionFailureCategory::Isolation,
				RuntimeSessionError::
					ResourceRoutingContractUnavailable,
				"editor_run.isolation.unavailable",
				"Editor-run resource-routing contract cannot be read",
				"resourceRoutingContract",
				sessionRoot /
					fs::u8path(
						ResourceRoutingContractFileName));
			return result;
		}
		ResourceRoutingContractParser
			resourceRoutingContractParser(
				resourceRoutingContractBytes);
		if (!resourceRoutingContractParser.parse())
		{
			fail(
				result,
				RuntimeSessionFailureCategory::Isolation,
				RuntimeSessionError::
					ResourceRoutingContractUnavailable,
				"editor_run.isolation.unavailable",
				"Editor-run resource-routing contract is invalid",
				"resourceRoutingContract",
				sessionRoot /
					fs::u8path(
						ResourceRoutingContractFileName));
			return result;
		}

		descriptor.overlayRoot = outputPaths[0];
		descriptor.isolatedSaveRoot = outputPaths[1];
		descriptor.applicationStateRoot =
			outputPaths[2];
		descriptor.diagnosticsPath =
			diagnosticsPath;
		descriptor.logPath = logPath;
		result.session.descriptor =
			std::move(descriptor);
		result.session.descriptorPath =
			descriptorPath;
		result.session.sessionRoot =
			sessionRoot;
		result.session.diagnosticsRoot =
			outputPaths[3];
		result.session.runtimeTracePath =
			runtimeTracePath;
		for (std::size_t index = 0;
			index < outputMetadata.size();
			++index)
		{
			result.session.outputDirectoryIdentities[index] =
				directoryIdentity(
					outputMetadata[index]);
		}
		result.session.traceOverlayOrigins =
			resourceRoutingContractParser.
				takeTraceOverlayOrigins();
		return result;
	}
	catch (const std::exception&)
	{
		fail(
			result,
			RuntimeSessionFailureCategory::
				DescriptorValidation,
			RuntimeSessionError::
				DescriptorPathInvalid,
			"editor_run.path.unsafe",
			"Editor-run descriptor path could not be normalized",
			"descriptorPath",
			requestedDescriptorPath);
		return result;
	}
}
}
