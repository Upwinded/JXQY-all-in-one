#include "DesktopRunSessionWorkspace.h"

#include "EditorAssetPath.h"
#include "SavedSceneLaunchPreparation.h"

#include "../../src/File/ResourcePathSafety.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QVector>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <aclapi.h>
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>
#ifdef _MSC_VER
#pragma comment(lib, "Advapi32.lib")
#endif
#else
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace
{
namespace fs = std::filesystem;

constexpr auto OverlayDirectoryName = "overlay";
constexpr auto OverlayStagingDirectoryName =
    ".overlay-staging";
constexpr auto SaveDirectoryName = "save";
constexpr auto ApplicationStateDirectoryName = "application-state";
constexpr auto DiagnosticsDirectoryName = "diagnostics";
constexpr auto MarkerFileName = "session-marker.json";
constexpr auto ResourceRoutingContractFileName =
    "resource-routing-contract.json";
constexpr auto DescriptorFileName = "launch-descriptor.json";
constexpr auto DiagnosticsFileName = "diagnostics.jsonl";
constexpr auto LogFileName = "game.log";
constexpr auto RuntimeTraceFileName =
    "runtime-trace.jsonl";
constexpr int MaximumSessionCreationAttempts = 32;
constexpr qsizetype HashBufferBytes = 1024 * 1024;

enum class NativeNodeKind
{
    RegularFile,
    Directory,
    Link
};

struct NativeMetadata
{
    NativeNodeKind kind = NativeNodeKind::RegularFile;
    quint64 device = 0;
    quint64 nodeHigh = 0;
    quint64 nodeLow = 0;
    quint64 size = 0;
    qint64 mtimeSeconds = 0;
    qint32 mtimeNanoseconds = 0;
    quint64 modeOrAttributes = 0;
    quint32 linkTag = 0;
    quint64 linkCount = 0;
};

class NativeHandle
{
public:
#ifdef Q_OS_WIN
    using Value = HANDLE;
#else
    using Value = int;
#endif

    static Value invalid()
    {
#ifdef Q_OS_WIN
        return INVALID_HANDLE_VALUE;
#else
        return -1;
#endif
    }

    NativeHandle() = default;
    explicit NativeHandle(Value value)
        : m_value(value)
    {
    }
    ~NativeHandle()
    {
        reset();
    }
    NativeHandle(const NativeHandle&) = delete;
    NativeHandle& operator=(const NativeHandle&) = delete;
    NativeHandle(NativeHandle&& other) noexcept
        : m_value(other.release())
    {
    }
    NativeHandle& operator=(NativeHandle&& other) noexcept
    {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    bool valid() const
    {
        return m_value != invalid();
    }
    Value get() const
    {
        return m_value;
    }
    Value release()
    {
        const Value value = m_value;
        m_value = invalid();
        return value;
    }
    void reset(Value value = invalid())
    {
        if (m_value != invalid())
        {
#ifdef Q_OS_WIN
            CloseHandle(m_value);
#else
            ::close(m_value);
#endif
        }
        m_value = value;
    }

private:
    Value m_value = invalid();
};

struct DirectoryAnchor
{
    QString canonicalPath;
    QByteArray canonicalPathUtf8;
    NativeMetadata metadata;
    NativeHandle handle;
};

struct FormalRoot
{
    QString path;
    QByteArray pathUtf8;
    bool resourceRole = false;
    bool saveRole = false;
};

enum class ScanFailure
{
    None,
    Read,
    Race,
    Limit
};

struct ScanContext
{
    DesktopRunSessionWorkspaceLimits limits;
    quint64 workingBytes = 0;
    ScanFailure failure = ScanFailure::None;
    QString problemPath;
    QString message;
};

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
DesktopRunSessionWorkspaceFaultInjector& faultInjector()
{
    static DesktopRunSessionWorkspaceFaultInjector injector;
    return injector;
}

std::mutex& faultInjectorMutex()
{
    static std::mutex mutex;
    return mutex;
}
#endif

bool injectFailure(
    DesktopRunSessionWorkspaceFaultPoint point,
    const QString& path)
{
#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
    DesktopRunSessionWorkspaceFaultInjector injector;
    {
        const std::lock_guard<std::mutex> lock(
            faultInjectorMutex());
        injector = faultInjector();
    }
    return injector && injector(point, path);
#else
    (void)point;
    (void)path;
    return false;
#endif
}

void setScanFailure(
    ScanContext& context,
    ScanFailure failure,
    const QString& path,
    const QString& message);
bool accountWorkingBytes(
    ScanContext& context,
    const QString& path,
    quint64 bytes);

QString normalizedAbsolutePath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString traceRootKindName(ResourceContentRoot::Kind kind)
{
    switch (kind)
    {
    case ResourceContentRoot::Kind::Local:
        return QStringLiteral("active");
    case ResourceContentRoot::Kind::DependencyId:
        return QStringLiteral("dependency-id");
    case ResourceContentRoot::Kind::Common:
        return QStringLiteral("common");
    }
    return {};
}

bool exactUtf8(const QString& text, QByteArray& bytes)
{
    bytes = text.toUtf8();
    return QString::fromUtf8(bytes.constData(), bytes.size()) == text;
}

bool pathText(const fs::path& path, QString& text, QByteArray& utf8)
{
#ifdef Q_OS_WIN
    text = QDir::fromNativeSeparators(
        QString::fromStdWString(path.native()));
    return exactUtf8(text, utf8);
#else
    const auto encoded = path.generic_u8string();
    utf8 = QByteArray(
        reinterpret_cast<const char*>(encoded.data()),
        static_cast<qsizetype>(encoded.size()));
    text = QString::fromUtf8(utf8.constData(), utf8.size());
    return text.toUtf8() == utf8;
#endif
}

bool hostPath(const QString& text, fs::path& path)
{
    QByteArray utf8;
    if (!exactUtf8(text, utf8))
        return false;
    try
    {
#ifdef Q_OS_WIN
        path = fs::path(
            reinterpret_cast<const wchar_t*>(text.utf16()));
#else
        path = fs::u8path(
            utf8.constData(),
            utf8.constData() + utf8.size());
#endif
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool sameIdentity(
    const NativeMetadata& left,
    const NativeMetadata& right)
{
    return left.kind == right.kind &&
        left.device == right.device &&
        left.nodeHigh == right.nodeHigh &&
        left.nodeLow == right.nodeLow;
}

bool sameStableMetadata(
    const NativeMetadata& left,
    const NativeMetadata& right)
{
    return sameIdentity(left, right) &&
        left.size == right.size &&
        left.mtimeSeconds == right.mtimeSeconds &&
        left.mtimeNanoseconds == right.mtimeNanoseconds &&
        left.modeOrAttributes == right.modeOrAttributes &&
        left.linkTag == right.linkTag &&
        left.linkCount == right.linkCount;
}

#ifdef Q_OS_WIN
void splitWindowsFileTime(
    const FILETIME& value,
    qint64& seconds,
    qint32& nanoseconds)
{
    ULARGE_INTEGER ticks;
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    constexpr quint64 UnixEpochTicks = 116444736000000000ULL;
    constexpr quint64 TicksPerSecond = 10000000ULL;
    if (ticks.QuadPart >= UnixEpochTicks)
    {
        const quint64 unixTicks = ticks.QuadPart - UnixEpochTicks;
        seconds = static_cast<qint64>(
            unixTicks / TicksPerSecond);
        nanoseconds = static_cast<qint32>(
            (unixTicks % TicksPerSecond) * 100ULL);
        return;
    }

    const quint64 ticksBeforeEpoch =
        UnixEpochTicks - ticks.QuadPart;
    seconds = -static_cast<qint64>(
        ticksBeforeEpoch / TicksPerSecond);
    const quint64 remainder =
        ticksBeforeEpoch % TicksPerSecond;
    if (remainder != 0)
    {
        --seconds;
        nanoseconds = static_cast<qint32>(
            (TicksPerSecond - remainder) * 100ULL);
    }
}

bool metadataFromWindowsHandle(
    HANDLE handle,
    NativeMetadata& metadata,
    QString& message)
{
    BY_HANDLE_FILE_INFORMATION information = {};
    if (!GetFileInformationByHandle(handle, &information))
    {
        message = QStringLiteral(
            "GetFileInformationByHandle failed with error %1")
            .arg(GetLastError());
        return false;
    }

    FILE_ATTRIBUTE_TAG_INFO tagInformation = {};
    if (!GetFileInformationByHandleEx(
            handle,
            FileAttributeTagInfo,
            &tagInformation,
            sizeof(tagInformation)))
    {
        message = QStringLiteral(
            "GetFileInformationByHandleEx failed with error %1")
            .arg(GetLastError());
        return false;
    }

    metadata.modeOrAttributes =
        tagInformation.FileAttributes;
    metadata.linkTag =
        (tagInformation.FileAttributes &
         FILE_ATTRIBUTE_REPARSE_POINT)
        ? tagInformation.ReparseTag
        : 0;
    if ((tagInformation.FileAttributes &
         FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        metadata.kind = NativeNodeKind::Link;
    }
    else if ((tagInformation.FileAttributes &
              FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        metadata.kind = NativeNodeKind::Directory;
    }
    else
    {
        metadata.kind = NativeNodeKind::RegularFile;
    }

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
        metadata.device = information.dwVolumeSerialNumber;
        metadata.nodeHigh = information.nFileIndexHigh;
        metadata.nodeLow = information.nFileIndexLow;
    }
    metadata.linkCount = information.nNumberOfLinks;
    metadata.size =
        (static_cast<quint64>(information.nFileSizeHigh) << 32) |
        information.nFileSizeLow;
    splitWindowsFileTime(
        information.ftLastWriteTime,
        metadata.mtimeSeconds,
        metadata.mtimeNanoseconds);
    return true;
}

bool queryNativeMetadata(
    const QString& path,
    NativeMetadata& metadata,
    QString& message)
{
    const std::wstring nativePath =
        QDir::toNativeSeparators(path).toStdWString();
    HANDLE handle = CreateFileW(
        nativePath.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        message = QStringLiteral(
            "Cannot open path metadata (system error %1)")
            .arg(GetLastError());
        return false;
    }
    const bool succeeded =
        metadataFromWindowsHandle(handle, metadata, message);
    CloseHandle(handle);
    return succeeded;
}
#else
void splitPosixFileTime(
    const struct stat& information,
    qint64& seconds,
    qint32& nanoseconds)
{
#if defined(Q_OS_MACOS)
    seconds = information.st_mtimespec.tv_sec;
    nanoseconds = static_cast<qint32>(
        information.st_mtimespec.tv_nsec);
#else
    seconds = information.st_mtim.tv_sec;
    nanoseconds = static_cast<qint32>(
        information.st_mtim.tv_nsec);
#endif
}

bool metadataFromPosixStat(
    const struct stat& information,
    NativeMetadata& metadata,
    QString& message)
{
    if (S_ISREG(information.st_mode))
    {
        metadata.kind = NativeNodeKind::RegularFile;
    }
    else if (S_ISDIR(information.st_mode))
    {
        metadata.kind = NativeNodeKind::Directory;
    }
    else if (S_ISLNK(information.st_mode))
    {
        metadata.kind = NativeNodeKind::Link;
    }
    else
    {
        message = QStringLiteral(
            "Unsupported filesystem node type");
        return false;
    }

    metadata.device = static_cast<quint64>(
        information.st_dev);
    metadata.nodeLow = static_cast<quint64>(
        information.st_ino);
    metadata.size = information.st_size < 0
        ? 0
        : static_cast<quint64>(information.st_size);
    metadata.modeOrAttributes = static_cast<quint64>(
        information.st_mode);
    metadata.linkCount = static_cast<quint64>(
        information.st_nlink);
    splitPosixFileTime(
        information,
        metadata.mtimeSeconds,
        metadata.mtimeNanoseconds);
    return true;
}

bool queryNativeMetadata(
    const QString& path,
    NativeMetadata& metadata,
    QString& message)
{
    const QByteArray nativePath = QFile::encodeName(path);
    struct stat information = {};
    if (::lstat(nativePath.constData(), &information) != 0)
    {
        message = QStringLiteral(
            "Cannot read path metadata (system error %1)")
            .arg(errno);
        return false;
    }
    return metadataFromPosixStat(
        information,
        metadata,
        message);
}
#endif

bool metadataFromNativeHandle(
    NativeHandle::Value handle,
    NativeMetadata& metadata,
    QString& message)
{
#ifdef Q_OS_WIN
    return metadataFromWindowsHandle(
        handle, metadata, message);
#else
    struct stat information = {};
    if (handle < 0 ||
        ::fstat(handle, &information) != 0)
    {
        message = QStringLiteral(
            "Cannot stat an anchored filesystem node (system error %1)")
            .arg(errno);
        return false;
    }
    return metadataFromPosixStat(
        information, metadata, message);
#endif
}

#ifdef Q_OS_WIN
using NtCreateFileFunction = decltype(&NtCreateFile);
using NtSetInformationFileFunction = NTSTATUS (NTAPI*)(
    HANDLE,
    PIO_STATUS_BLOCK,
    PVOID,
    ULONG,
    FILE_INFORMATION_CLASS);

NtCreateFileFunction nativeNtCreateFile()
{
    static const NtCreateFileFunction function = []()
    {
        const HMODULE ntdll =
            GetModuleHandleW(L"ntdll.dll");
        const FARPROC address = ntdll != nullptr
            ? GetProcAddress(
                  ntdll, "NtCreateFile")
            : nullptr;
        NtCreateFileFunction loaded = nullptr;
        static_assert(
            sizeof(loaded) == sizeof(address));
        std::memcpy(
            &loaded, &address, sizeof(loaded));
        return loaded;
    }();
    return function;
}

NtSetInformationFileFunction nativeNtSetInformationFile()
{
    static const NtSetInformationFileFunction function = []()
    {
        const HMODULE ntdll =
            GetModuleHandleW(L"ntdll.dll");
        const FARPROC address = ntdll != nullptr
            ? GetProcAddress(
                  ntdll, "NtSetInformationFile")
            : nullptr;
        NtSetInformationFileFunction loaded = nullptr;
        static_assert(
            sizeof(loaded) == sizeof(address));
        std::memcpy(
            &loaded, &address, sizeof(loaded));
        return loaded;
    }();
    return function;
}

bool ntOpenRelative(
    HANDLE parent,
    const std::wstring& leaf,
    ACCESS_MASK desiredAccess,
    ULONG disposition,
    ULONG createOptions,
    ULONG fileAttributes,
    NativeHandle& opened,
    ULONG_PTR* informationValue,
    QString& message)
{
    opened.reset();
    const NtCreateFileFunction function =
        nativeNtCreateFile();
    if (function == nullptr ||
        parent == INVALID_HANDLE_VALUE ||
        leaf.empty() ||
        leaf == L"." ||
        leaf == L".." ||
        leaf.find_first_of(L"\\/") !=
            std::wstring::npos ||
        leaf.size() >
            static_cast<std::size_t>(
                (std::numeric_limits<USHORT>::max)()) /
                sizeof(wchar_t))
    {
        message = QStringLiteral(
            "Invalid anchored Windows path component");
        return false;
    }

    UNICODE_STRING leafName = {};
    leafName.Buffer =
        const_cast<PWSTR>(leaf.data());
    leafName.Length = static_cast<USHORT>(
        leaf.size() * sizeof(wchar_t));
    leafName.MaximumLength = leafName.Length;
    OBJECT_ATTRIBUTES attributes = {};
    InitializeObjectAttributes(
        &attributes,
        &leafName,
        0,
        parent,
        nullptr);
    IO_STATUS_BLOCK ioStatus = {};
    HANDLE value = INVALID_HANDLE_VALUE;
    const NTSTATUS status = function(
        &value,
        desiredAccess,
        &attributes,
        &ioStatus,
        nullptr,
        fileAttributes,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        disposition,
        createOptions |
            FILE_OPEN_REPARSE_POINT |
            FILE_SYNCHRONOUS_IO_NONALERT,
        nullptr,
        0);
    if (status < 0 ||
        value == INVALID_HANDLE_VALUE)
    {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
        message = QStringLiteral(
            "Anchored NtCreateFile failed with status 0x%1")
            .arg(
                static_cast<quint32>(status),
                8,
                16,
                QLatin1Char('0'));
        return false;
    }
    if (informationValue != nullptr)
        *informationValue = ioStatus.Information;
    opened.reset(value);
    return true;
}

bool openAbsoluteDirectoryHandle(
    const QString& path,
    bool writableSessionsBase,
    NativeHandle& handle,
    QString& message)
{
    const std::wstring nativePath =
        QDir::toNativeSeparators(path)
            .toStdWString();
    DWORD access =
        FILE_LIST_DIRECTORY |
        FILE_READ_ATTRIBUTES |
        READ_CONTROL |
        SYNCHRONIZE;
    if (writableSessionsBase)
    {
        access |=
            FILE_ADD_FILE |
            FILE_ADD_SUBDIRECTORY |
            FILE_WRITE_ATTRIBUTES |
            FILE_DELETE_CHILD;
    }
    HANDLE value = CreateFileW(
        nativePath.c_str(),
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (value == INVALID_HANDLE_VALUE)
    {
        message = QStringLiteral(
            "Cannot open anchored directory (system error %1)")
            .arg(GetLastError());
        return false;
    }
    handle.reset(value);
    return true;
}

bool trustedWindowsDirectoryPermissions(
    HANDLE handle,
    QString& message)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &token))
    {
        message = QStringLiteral(
            "Cannot inspect the current Windows user token");
        return false;
    }
    DWORD tokenBytes = 0;
    GetTokenInformation(
        token, TokenUser, nullptr, 0, &tokenBytes);
    std::vector<unsigned char> tokenStorage(
        tokenBytes);
    if (tokenBytes == 0 ||
        !GetTokenInformation(
            token,
            TokenUser,
            tokenStorage.data(),
            tokenBytes,
            &tokenBytes))
    {
        CloseHandle(token);
        message = QStringLiteral(
            "Cannot inspect the current Windows user SID");
        return false;
    }
    const PSID currentUser =
        reinterpret_cast<TOKEN_USER*>(
            tokenStorage.data())->User.Sid;

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    const DWORD securityResult = GetSecurityInfo(
        handle,
        SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION |
            DACL_SECURITY_INFORMATION,
        &owner,
        nullptr,
        &dacl,
        nullptr,
        &descriptor);
    if (securityResult != ERROR_SUCCESS ||
        owner == nullptr ||
        !EqualSid(owner, currentUser) ||
        dacl == nullptr)
    {
        if (descriptor != nullptr)
            LocalFree(descriptor);
        CloseHandle(token);
        message = QStringLiteral(
            "Sessions base must be owned by the current user and have a DACL");
        return false;
    }

    constexpr ACCESS_MASK SensitiveAccess =
        FILE_LIST_DIRECTORY |
        FILE_ADD_FILE |
        FILE_ADD_SUBDIRECTORY |
        FILE_READ_EA |
        FILE_WRITE_EA |
        FILE_TRAVERSE |
        FILE_READ_ATTRIBUTES |
        FILE_WRITE_ATTRIBUTES |
        FILE_DELETE_CHILD |
        DELETE |
        READ_CONTROL |
        WRITE_DAC |
        WRITE_OWNER |
        GENERIC_READ |
        GENERIC_WRITE |
        GENERIC_EXECUTE |
        GENERIC_ALL;
    bool safe = true;
    for (DWORD index = 0;
         index < dacl->AceCount;
         ++index)
    {
        void* rawAce = nullptr;
        if (!GetAce(dacl, index, &rawAce))
        {
            safe = false;
            break;
        }
        const auto* header =
            static_cast<ACE_HEADER*>(rawAce);
        if (header->AceType !=
            ACCESS_ALLOWED_ACE_TYPE)
        {
            if (header->AceType ==
                    ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
                header->AceType ==
                    ACCESS_ALLOWED_CALLBACK_ACE_TYPE ||
                header->AceType ==
                    ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE)
            {
                safe = false;
                break;
            }
            continue;
        }
        const auto* ace =
            static_cast<ACCESS_ALLOWED_ACE*>(
                rawAce);
        if ((ace->Mask & SensitiveAccess) == 0)
            continue;
        const PSID sid = const_cast<DWORD*>(
            &ace->SidStart);
        const bool trusted =
            EqualSid(sid, currentUser) ||
            IsWellKnownSid(
                sid, WinLocalSystemSid) ||
            IsWellKnownSid(
                sid, WinBuiltinAdministratorsSid) ||
            IsWellKnownSid(
                sid, WinCreatorOwnerSid);
        if (!trusted)
        {
            safe = false;
            break;
        }
    }
    LocalFree(descriptor);
    CloseHandle(token);
    if (!safe)
    {
        message = QStringLiteral(
            "Sessions base grants write access to an untrusted Windows SID");
        return false;
    }
    return true;
}

bool enumerateDirectoryNames(
    HANDLE directory,
    QVector<QString>& names,
    ScanContext& context,
    const QString& displayPath,
    quint64 maximumNames)
{
    names.clear();
    QByteArray buffer(64 * 1024, Qt::Uninitialized);
    bool restart = true;
    while (true)
    {
        const FILE_INFO_BY_HANDLE_CLASS informationClass =
            restart
            ? FileIdBothDirectoryRestartInfo
            : FileIdBothDirectoryInfo;
        if (!GetFileInformationByHandleEx(
                directory,
                informationClass,
                buffer.data(),
                static_cast<DWORD>(buffer.size())))
        {
            const DWORD error = GetLastError();
            if (error == ERROR_NO_MORE_FILES)
                return true;
            setScanFailure(
                context,
                ScanFailure::Read,
                displayPath,
                QStringLiteral(
                    "Cannot enumerate an anchored directory "
                    "(system error %1)")
                    .arg(error));
            return false;
        }
        restart = false;
        DWORD offset = 0;
        while (true)
        {
            if (offset +
                    offsetof(
                        FILE_ID_BOTH_DIR_INFO,
                        FileName) >
                static_cast<DWORD>(
                    buffer.size()))
            {
                setScanFailure(
                    context,
                    ScanFailure::Read,
                    displayPath,
                    QStringLiteral(
                        "Windows directory enumeration data is truncated"));
                return false;
            }
            const auto* entry =
                reinterpret_cast<
                    const FILE_ID_BOTH_DIR_INFO*>(
                    buffer.constData() + offset);
            if ((entry->FileNameLength %
                 sizeof(wchar_t)) != 0 ||
                offset +
                        offsetof(
                            FILE_ID_BOTH_DIR_INFO,
                            FileName) +
                        entry->FileNameLength >
                    static_cast<DWORD>(
                        buffer.size()))
            {
                setScanFailure(
                    context,
                    ScanFailure::Read,
                    displayPath,
                    QStringLiteral(
                        "Windows directory entry name is invalid"));
                return false;
            }
            const QString name =
                QString::fromWCharArray(
                    entry->FileName,
                    static_cast<qsizetype>(
                        entry->FileNameLength /
                        sizeof(wchar_t)));
            if (name != QStringLiteral(".") &&
                name != QStringLiteral(".."))
            {
                QByteArray nameUtf8;
                if (!exactUtf8(name, nameUtf8) ||
                    name.isEmpty() ||
                    name.contains('/') ||
                    name.contains('\\') ||
                    name.contains(QChar::Null) ||
                    nameUtf8.size() >
                        context.limits.maximumPathBytes)
                {
                    setScanFailure(
                        context,
                        ScanFailure::Read,
                        displayPath,
                        QStringLiteral(
                            "Directory contains an invalid or overlong UTF-8 entry name"));
                    return false;
                }
                if (static_cast<quint64>(
                        names.size()) >=
                    maximumNames)
                {
                    setScanFailure(
                        context,
                        ScanFailure::Limit,
                        displayPath,
                        QStringLiteral(
                            "Directory entry count exceeds the configured limit during enumeration"));
                    return false;
                }
                const quint64 charge =
                    sizeof(QString) +
                    static_cast<quint64>(
                        name.size()) *
                        sizeof(QChar) * 2ULL +
                    static_cast<quint64>(
                        nameUtf8.size()) * 2ULL;
                if (!accountWorkingBytes(
                        context,
                        displayPath,
                        charge))
                {
                    return false;
                }
                names.append(name);
            }
            if (entry->NextEntryOffset == 0)
                break;
            offset += entry->NextEntryOffset;
            if (offset >=
                static_cast<DWORD>(
                    buffer.size()))
            {
                setScanFailure(
                    context,
                    ScanFailure::Read,
                    displayPath,
                    QStringLiteral(
                        "Windows directory entry offset is invalid"));
                return false;
            }
        }
    }
}
#else
bool openAbsoluteDirectoryHandle(
    const QString& path,
    bool,
    NativeHandle& handle,
    QString& message)
{
    const QByteArray nativePath =
        QFile::encodeName(path);
    const int value = ::open(
        nativePath.constData(),
        O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW);
    if (value < 0)
    {
        message = QStringLiteral(
            "Cannot open anchored directory (system error %1)")
            .arg(errno);
        return false;
    }
    handle.reset(value);
    return true;
}

bool trustedPosixDirectoryPermissions(
    int descriptor,
    QString& message)
{
    struct stat information = {};
    if (::fstat(descriptor, &information) != 0)
    {
        message = QStringLiteral(
            "Cannot stat the trusted sessions base (system error %1)")
            .arg(errno);
        return false;
    }
    if (information.st_uid != ::geteuid() ||
        (information.st_mode & 0077) != 0 ||
        (information.st_mode & 0700) != 0700)
    {
        message = QStringLiteral(
            "Sessions base must be owned by the current user with mode 0700");
        return false;
    }
    return true;
}

bool enumerateDirectoryNames(
    int directory,
    QVector<QString>& names,
    ScanContext& context,
    const QString& displayPath,
    quint64 maximumNames)
{
    names.clear();
    const int enumerationDescriptor = ::openat(
        directory,
        ".",
        O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW);
    if (enumerationDescriptor < 0)
    {
        setScanFailure(
            context,
            ScanFailure::Read,
            displayPath,
            QStringLiteral(
                "Cannot duplicate an anchored directory "
                "(system error %1)")
                .arg(errno));
        return false;
    }
    DIR* stream = ::fdopendir(
        enumerationDescriptor);
    if (stream == nullptr)
    {
        const int error = errno;
        ::close(enumerationDescriptor);
        setScanFailure(
            context,
            ScanFailure::Read,
            displayPath,
            QStringLiteral(
                "Cannot enumerate an anchored directory "
                "(system error %1)")
                .arg(error));
        return false;
    }
    errno = 0;
    while (dirent* entry = ::readdir(stream))
    {
        const QByteArray nameBytes(entry->d_name);
        if (nameBytes == "." ||
            nameBytes == "..")
        {
            continue;
        }
        const QString name = QString::fromUtf8(
            nameBytes.constData(),
            nameBytes.size());
        if (name.toUtf8() != nameBytes)
        {
            ::closedir(stream);
            setScanFailure(
                context,
                ScanFailure::Read,
                displayPath,
                QStringLiteral(
                    "Directory contains an entry name that is not valid UTF-8"));
            return false;
        }
        if (name.isEmpty() ||
            name.contains('/') ||
            name.contains('\\') ||
            name.contains(QChar::Null) ||
            nameBytes.size() >
                context.limits.maximumPathBytes)
        {
            ::closedir(stream);
            setScanFailure(
                context,
                ScanFailure::Read,
                displayPath,
                QStringLiteral(
                    "Directory contains an invalid or overlong UTF-8 entry name"));
            return false;
        }
        if (static_cast<quint64>(
                names.size()) >=
                maximumNames)
        {
            ::closedir(stream);
            setScanFailure(
                context,
                ScanFailure::Limit,
                displayPath,
                QStringLiteral(
                    "Directory entry count exceeds the configured limit during enumeration"));
            return false;
        }
        const quint64 charge =
            sizeof(QString) +
            static_cast<quint64>(
                name.size()) *
                sizeof(QChar) * 2ULL +
            static_cast<quint64>(
                nameBytes.size()) * 2ULL;
        if (!accountWorkingBytes(
                context,
                displayPath,
                charge))
        {
            ::closedir(stream);
            return false;
        }
        names.append(name);
        errno = 0;
    }
    const int readError = errno;
    ::closedir(stream);
    if (readError != 0)
    {
        setScanFailure(
            context,
            ScanFailure::Read,
            displayPath,
            QStringLiteral(
                "Cannot completely enumerate an anchored directory "
                "(system error %1)")
                .arg(readError));
        return false;
    }
    return true;
}
#endif

bool pathComponentIsSafe(
    const QString& name,
    QByteArray& utf8)
{
    return exactUtf8(name, utf8) &&
        !name.isEmpty() &&
        name != QStringLiteral(".") &&
        name != QStringLiteral("..") &&
        !name.contains('/') &&
        !name.contains('\\') &&
        !name.contains(QChar::Null);
}

bool anchoredPathStillHasIdentity(
    const QString& path,
    const NativeMetadata& expected)
{
    NativeMetadata current;
    QString message;
    return queryNativeMetadata(
               path, current, message) &&
        sameIdentity(expected, current);
}

void setScanFailure(
    ScanContext& context,
    ScanFailure failure,
    const QString& path,
    const QString& message)
{
    if (context.failure != ScanFailure::None)
        return;
    context.failure = failure;
    context.problemPath = path;
    context.message = message;
}

bool accountWorkingBytes(
    ScanContext& context,
    const QString& path,
    quint64 bytes)
{
    if (bytes >
            context.limits.maximumWorkingBytes ||
        context.workingBytes >
            context.limits.maximumWorkingBytes -
                bytes)
    {
        setScanFailure(
            context,
            ScanFailure::Limit,
            path,
            QStringLiteral(
                "Directory enumeration working memory exceeds the configured limit"));
        return false;
    }
    context.workingBytes += bytes;
    return true;
}

struct ListedChild
{
    QString name;
    QByteArray nameUtf8;
    QString displayPath;
    NativeMetadata metadata;
    NativeHandle handle;
    QString linkTarget;
};

bool openAnchoredChild(
    NativeHandle::Value parent,
    ListedChild& child,
    ScanContext& context)
{
#ifdef Q_OS_WIN
    const std::wstring component =
        child.name.toStdWString();
    QString openMessage;
    NativeHandle opened;
    if (!ntOpenRelative(
            parent,
            component,
            FILE_LIST_DIRECTORY |
                FILE_READ_ATTRIBUTES |
                SYNCHRONIZE,
            FILE_OPEN,
            FILE_DIRECTORY_FILE,
            FILE_ATTRIBUTE_NORMAL,
            opened,
            nullptr,
            openMessage))
    {
        openMessage.clear();
        if (!ntOpenRelative(
                parent,
                component,
                FILE_READ_DATA |
                    FILE_READ_ATTRIBUTES |
                    SYNCHRONIZE,
                FILE_OPEN,
                FILE_NON_DIRECTORY_FILE,
                FILE_ATTRIBUTE_NORMAL,
                opened,
                nullptr,
                openMessage) &&
            !ntOpenRelative(
                parent,
                component,
                FILE_READ_ATTRIBUTES |
                    SYNCHRONIZE,
                FILE_OPEN,
                0,
                FILE_ATTRIBUTE_NORMAL,
                opened,
                nullptr,
                openMessage))
        {
            setScanFailure(
                context,
                ScanFailure::Read,
                child.displayPath,
                openMessage);
            return false;
        }
    }
    QString metadataMessage;
    if (!metadataFromNativeHandle(
            opened.get(),
            child.metadata,
            metadataMessage))
    {
        setScanFailure(
            context,
            ScanFailure::Read,
            child.displayPath,
            metadataMessage);
        return false;
    }
    child.handle = std::move(opened);
    if (child.metadata.kind != NativeNodeKind::Link)
        return true;

    std::array<unsigned char,
               MAXIMUM_REPARSE_DATA_BUFFER_SIZE> buffer = {};
    DWORD bytesReturned = 0;
    const BOOL readSucceeded = DeviceIoControl(
        child.handle.get(),
        FSCTL_GET_REPARSE_POINT,
        nullptr,
        0,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        &bytesReturned,
        nullptr);
    const DWORD readError =
        readSucceeded ? ERROR_SUCCESS : GetLastError();
    if (!readSucceeded || bytesReturned < 8)
    {
        setScanFailure(
            context,
            ScanFailure::Read,
            child.displayPath,
            QStringLiteral(
                "Cannot read non-followed link metadata "
                "(system error %1)")
                .arg(readError));
        return false;
    }

    auto readWord =
        [&buffer, bytesReturned](
            std::size_t offset,
            WORD& value)
        {
            if (offset + sizeof(value) >
                bytesReturned)
            {
                return false;
            }
            std::memcpy(
                &value,
                buffer.data() + offset,
                sizeof(value));
            return true;
        };
    DWORD tag = 0;
    std::memcpy(
        &tag, buffer.data(), sizeof(tag));
    if (tag != IO_REPARSE_TAG_MOUNT_POINT &&
        tag != IO_REPARSE_TAG_SYMLINK)
    {
        // The reparse tag is already serialized. Unknown reparse payloads
        // remain opaque and are never opened or followed.
        child.linkTarget.clear();
        return true;
    }

    WORD substituteOffset = 0;
    WORD substituteLength = 0;
    WORD printOffset = 0;
    WORD printLength = 0;
    if (!readWord(8, substituteOffset) ||
        !readWord(10, substituteLength) ||
        !readWord(12, printOffset) ||
        !readWord(14, printLength))
    {
        setScanFailure(
            context,
            ScanFailure::Read,
            child.displayPath,
            QStringLiteral(
                "Non-followed link metadata is truncated"));
        return false;
    }

    const std::size_t pathBufferOffset =
        tag == IO_REPARSE_TAG_SYMLINK
        ? 20
        : 16;
    WORD selectedOffset = printOffset;
    WORD selectedLength = printLength;
    bool usedSubstituteName = false;
    if (selectedLength == 0)
    {
        selectedOffset = substituteOffset;
        selectedLength = substituteLength;
        usedSubstituteName = true;
    }
    if ((selectedLength % sizeof(wchar_t)) != 0 ||
        pathBufferOffset + selectedOffset +
                selectedLength >
            bytesReturned)
    {
        setScanFailure(
            context,
            ScanFailure::Read,
            child.displayPath,
            QStringLiteral(
                "Non-followed link target metadata is invalid"));
        return false;
    }

    std::wstring targetText(
        selectedLength / sizeof(wchar_t),
        L'\0');
    if (selectedLength != 0)
    {
        std::memcpy(
            targetText.data(),
            buffer.data() +
                pathBufferOffset +
                selectedOffset,
            selectedLength);
    }
    child.linkTarget =
        QString::fromStdWString(targetText);
    if (usedSubstituteName &&
        child.linkTarget.startsWith(
            QStringLiteral("\\??\\UNC\\")))
    {
        child.linkTarget =
            QStringLiteral("\\\\") +
            child.linkTarget.sliced(8);
    }
    else if (usedSubstituteName &&
             child.linkTarget.startsWith(
                 QStringLiteral("\\??\\")))
    {
        child.linkTarget.remove(0, 4);
    }
    QByteArray targetUtf8;
    if (!exactUtf8(
            child.linkTarget,
            targetUtf8) ||
        targetUtf8.size() >
            context.limits.maximumPathBytes)
    {
        setScanFailure(
            context,
            ScanFailure::Read,
            child.displayPath,
            QStringLiteral(
                "Link target is not valid bounded UTF-8"));
        return false;
    }
    return true;
#else
    const QByteArray component =
        QFile::encodeName(child.name);
    struct stat initialInformation = {};
    if (::fstatat(
            parent,
            component.constData(),
            &initialInformation,
            AT_SYMLINK_NOFOLLOW) != 0)
    {
        setScanFailure(
            context,
            ScanFailure::Read,
            child.displayPath,
            QStringLiteral(
                "Cannot stat an anchored directory entry (system error %1)")
                .arg(errno));
        return false;
    }
    QString metadataMessage;
    if (!metadataFromPosixStat(
            initialInformation,
            child.metadata,
            metadataMessage))
    {
        setScanFailure(
            context,
            ScanFailure::Read,
            child.displayPath,
            metadataMessage);
        return false;
    }
    if (child.metadata.kind ==
        NativeNodeKind::Link)
    {
        qsizetype capacity = static_cast<qsizetype>(
            std::min<quint64>(
                std::max<quint64>(
                    child.metadata.size + 1,
                    256),
                static_cast<quint64>(
                    context.limits.maximumPathBytes)));
        QByteArray targetBytes(
            capacity, Qt::Uninitialized);
        while (true)
        {
            const ssize_t length = ::readlinkat(
                parent,
                component.constData(),
                targetBytes.data(),
                static_cast<std::size_t>(
                    targetBytes.size()));
            if (length < 0)
            {
                setScanFailure(
                    context,
                    ScanFailure::Read,
                    child.displayPath,
                    QStringLiteral(
                        "Cannot read an anchored directory link "
                        "(system error %1)")
                        .arg(errno));
                return false;
            }
            if (length <
                targetBytes.size())
            {
                targetBytes.truncate(
                    static_cast<qsizetype>(
                        length));
                break;
            }
            if (targetBytes.size() >=
                context.limits.maximumPathBytes)
            {
                setScanFailure(
                    context,
                    ScanFailure::Limit,
                    child.displayPath,
                    QStringLiteral(
                        "Directory link target exceeds the configured path limit"));
                return false;
            }
            targetBytes.resize(
                std::min<qsizetype>(
                    context.limits.maximumPathBytes,
                    targetBytes.size() * 2));
        }
        child.linkTarget = QString::fromUtf8(
            targetBytes.constData(),
            targetBytes.size());
        if (child.linkTarget.toUtf8() !=
            targetBytes)
        {
            setScanFailure(
                context,
                ScanFailure::Read,
                child.displayPath,
                QStringLiteral(
                    "Link target is not valid UTF-8"));
            return false;
        }
        struct stat afterInformation = {};
        NativeMetadata afterMetadata;
        if (::fstatat(
                parent,
                component.constData(),
                &afterInformation,
                AT_SYMLINK_NOFOLLOW) != 0 ||
            !metadataFromPosixStat(
                afterInformation,
                afterMetadata,
                metadataMessage) ||
            !sameStableMetadata(
                child.metadata,
                afterMetadata))
        {
            setScanFailure(
                context,
                ScanFailure::Race,
                child.displayPath,
                QStringLiteral(
                    "Directory link changed while reading metadata"));
            return false;
        }
        return true;
    }

    const int flags =
        O_RDONLY |
        O_CLOEXEC |
        O_NOFOLLOW |
        (child.metadata.kind ==
                 NativeNodeKind::Directory
             ? O_DIRECTORY
             : 0);
    const int opened = ::openat(
        parent, component.constData(), flags);
    if (opened < 0)
    {
        setScanFailure(
            context,
            ScanFailure::Read,
            child.displayPath,
            QStringLiteral(
                "Cannot open an anchored directory entry (system error %1)")
                .arg(errno));
        return false;
    }
    child.handle.reset(opened);
    NativeMetadata openedMetadata;
    if (!metadataFromNativeHandle(
            child.handle.get(),
            openedMetadata,
            metadataMessage) ||
        !sameStableMetadata(
            child.metadata,
            openedMetadata))
    {
        setScanFailure(
            context,
            ScanFailure::Race,
            child.displayPath,
            QStringLiteral(
                "Directory entry changed while it was opened"));
        return false;
    }
    return true;
#endif
}

bool anchoredChildStillMatches(
    NativeHandle::Value parent,
    const ListedChild& child)
{
#ifdef Q_OS_WIN
    NativeHandle reopened;
    QString message;
    if (!ntOpenRelative(
            parent,
            child.name.toStdWString(),
            FILE_READ_ATTRIBUTES |
                SYNCHRONIZE,
            FILE_OPEN,
            0,
            FILE_ATTRIBUTE_NORMAL,
            reopened,
            nullptr,
            message))
    {
        return false;
    }
    NativeMetadata metadata;
    return metadataFromNativeHandle(
               reopened.get(), metadata, message) &&
        sameStableMetadata(
            child.metadata, metadata);
#else
    const QByteArray component =
        QFile::encodeName(child.name);
    struct stat information = {};
    NativeMetadata metadata;
    QString message;
    return ::fstatat(
               parent,
               component.constData(),
               &information,
               AT_SYMLINK_NOFOLLOW) == 0 &&
        metadataFromPosixStat(
            information, metadata, message) &&
        sameStableMetadata(
            child.metadata, metadata);
#endif
}

bool appendJsonString(
    const QString& value,
    QByteArray& output)
{
    QByteArray utf8;
    if (!exactUtf8(value, utf8))
        return false;

    constexpr char HexDigits[] = "0123456789abcdef";
    output.append('"');
    for (const unsigned char character : utf8)
    {
        switch (character)
        {
        case '"':
            output.append("\\\"");
            break;
        case '\\':
            output.append("\\\\");
            break;
        case '\b':
            output.append("\\b");
            break;
        case '\f':
            output.append("\\f");
            break;
        case '\n':
            output.append("\\n");
            break;
        case '\r':
            output.append("\\r");
            break;
        case '\t':
            output.append("\\t");
            break;
        default:
            if (character < 0x20)
            {
                output.append("\\u00");
                output.append(
                    HexDigits[(character >> 4) & 0x0F]);
                output.append(
                    HexDigits[character & 0x0F]);
            }
            else
            {
                output.append(
                    static_cast<char>(character));
            }
            break;
        }
    }
    output.append('"');
    return true;
}

bool serializeResourceRoutingContract(
    const std::vector<FormalRoot>& roots,
    const QVector<DesktopRunTraceOverlayOrigin>&
        traceOverlayOrigins,
    const DesktopRunSessionWorkspaceLimits& limits,
    QByteArray& output,
    QString& problemPath,
    QString& message)
{
    output.clear();
    output.append(
        "{\"schemaVersion\":1,\"roots\":[");
    const auto withinLimit =
        [&]()
        {
            if (output.size() <=
                limits.maximumManifestBytes)
            {
                return true;
            }
            problemPath.clear();
            message = QStringLiteral(
                "Desktop-run resource-routing contract exceeds the configured byte limit");
            return false;
        };
    for (std::size_t rootIndex = 0;
         rootIndex < roots.size();
         ++rootIndex)
    {
        if (rootIndex != 0)
            output.append(',');
        const FormalRoot& root = roots[rootIndex];
        output.append("{\"path\":");
        if (!appendJsonString(
                QDir::fromNativeSeparators(
                    root.path),
                output))
        {
            problemPath = root.path;
            message = QStringLiteral(
                "Formal root path is not valid UTF-8");
            return false;
        }
        output.append(",\"roles\":[");
        bool wroteRole = false;
        if (root.resourceRole)
        {
            output.append("\"resource\"");
            wroteRole = true;
        }
        if (root.saveRole)
        {
            if (wroteRole)
                output.append(',');
            output.append("\"save\"");
        }
        output.append("]}");
        if (!withinLimit())
            return false;
    }
    output.append(']');
    if (!traceOverlayOrigins.isEmpty())
    {
        output.append(",\"traceOverlayOrigins\":[");
        for (qsizetype index = 0;
             index < traceOverlayOrigins.size();
             ++index)
        {
            if (index != 0)
                output.append(',');
            const DesktopRunTraceOverlayOrigin& origin =
                traceOverlayOrigins.at(index);
            output.append("{\"virtualPath\":");
            if (!appendJsonString(
                    origin.virtualPath,
                    output))
            {
                problemPath = origin.virtualPath;
                message = QStringLiteral(
                    "Trace overlay origin path is not valid UTF-8");
                return false;
            }
            output.append(",\"rootOrdinal\":");
            output.append(
                QByteArray::number(
                    origin.rootOrdinal));
            output.append(",\"rootKind\":");
            if (!appendJsonString(
                    origin.rootKind,
                    output))
            {
                problemPath = origin.virtualPath;
                message = QStringLiteral(
                    "Trace overlay origin root kind is not valid UTF-8");
                return false;
            }
            output.append(",\"resourcePackId\":");
            if (!appendJsonString(
                    origin.resourcePackId,
                    output))
            {
                problemPath = origin.virtualPath;
                message = QStringLiteral(
                    "Trace overlay origin resource-pack ID is not valid UTF-8");
                return false;
            }
            output.append(",\"rootPath\":");
            if (!appendJsonString(
                    origin.rootPath,
                    output))
            {
                problemPath = origin.rootPath;
                message = QStringLiteral(
                    "Trace overlay origin root path is not valid UTF-8");
                return false;
            }
            output.append('}');
            if (!withinLimit())
                return false;
        }
        output.append(']');
    }
    output.append('}');
    return withinLimit();
}

void fail(
    DesktopRunSessionWorkspaceResult& result,
    DesktopRunSessionWorkspaceError error,
    const QString& problemPath,
    const QString& message)
{
    result.error = error;
    result.problemPath = problemPath;
    result.message = message;
}

bool validatePrivateRoot(
    const QString& rootPath,
    const DesktopRunSessionWorkspaceLimits& limits,
    DirectoryAnchor& anchor,
    DesktopRunSessionWorkspaceResult& result)
{
    if (rootPath.isEmpty() ||
        !QDir::isAbsolutePath(rootPath))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                InvalidSessionsBase,
            rootPath,
            QStringLiteral(
                "Root path must be an existing absolute directory"));
        return false;
    }
    const QString absolute =
        normalizedAbsolutePath(rootPath);
    NativeMetadata metadata;
    QString metadataMessage;
    if (!queryNativeMetadata(
            absolute, metadata, metadataMessage))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                InvalidSessionsBase,
            absolute,
            metadataMessage);
        return false;
    }
    if (metadata.kind == NativeNodeKind::Link)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                SessionsBaseIsLink,
            absolute,
            QStringLiteral(
                "Root path must not be a link or reparse point"));
        return false;
    }
    if (metadata.kind != NativeNodeKind::Directory)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                InvalidSessionsBase,
            absolute,
            QStringLiteral(
                "Root path is not a directory"));
        return false;
    }

    anchor.canonicalPath =
        QFileInfo(absolute).canonicalFilePath();
    if (anchor.canonicalPath.isEmpty())
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                InvalidSessionsBase,
            absolute,
            QStringLiteral(
                "Root path cannot be canonicalized"));
        return false;
    }
    anchor.canonicalPath =
        QDir::cleanPath(anchor.canonicalPath);
    if (!exactUtf8(
            anchor.canonicalPath,
            anchor.canonicalPathUtf8) ||
        anchor.canonicalPathUtf8.size() >
            limits.maximumPathBytes)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                InvalidSessionsBase,
            anchor.canonicalPath,
            QStringLiteral(
                "Root path is not valid bounded UTF-8"));
        return false;
    }

    if (!openAbsoluteDirectoryHandle(
            anchor.canonicalPath,
            true,
            anchor.handle,
            metadataMessage) ||
        !metadataFromNativeHandle(
            anchor.handle.get(),
            anchor.metadata,
            metadataMessage))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                InvalidSessionsBase,
            anchor.canonicalPath,
            metadataMessage);
        return false;
    }
    if (!sameStableMetadata(
            metadata, anchor.metadata))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                InvalidSessionsBase,
            anchor.canonicalPath,
            QStringLiteral(
                "Root path changed while being validated"));
        return false;
    }
#ifdef Q_OS_WIN
    const bool trustedPermissions =
        trustedWindowsDirectoryPermissions(
            anchor.handle.get(),
            metadataMessage);
#else
    const bool trustedPermissions =
        trustedPosixDirectoryPermissions(
            anchor.handle.get(),
            metadataMessage);
#endif
    if (!trustedPermissions)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                UntrustedSessionsBasePermissions,
            anchor.canonicalPath,
            metadataMessage);
        return false;
    }
    return true;
}

bool writeAtomically(
    NativeHandle::Value parent,
    const QString& fileName,
    const QString& path,
    const QByteArray& bytes,
    QString& message)
{
    QByteArray fileNameUtf8;
    if (!pathComponentIsSafe(
            fileName, fileNameUtf8))
    {
        message = QStringLiteral(
            "Atomic output name is not a safe path component");
        return false;
    }
    const QString temporaryName =
        QStringLiteral(".%1.%2.tmp")
            .arg(
                fileName,
                QUuid::createUuid()
                    .toString(
                        QUuid::WithoutBraces));
    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                BeforeAtomicTemporaryOpen,
            path))
    {
        message = QStringLiteral(
            "Injected failure before atomic temporary open");
        return false;
    }
#ifdef Q_OS_WIN
    NativeHandle temporary;
    ULONG_PTR createInformation = 0;
    if (!ntOpenRelative(
            parent,
            temporaryName.toStdWString(),
            FILE_WRITE_DATA |
                FILE_READ_ATTRIBUTES |
                DELETE |
                SYNCHRONIZE,
            FILE_CREATE,
            FILE_NON_DIRECTORY_FILE,
            FILE_ATTRIBUTE_TEMPORARY,
            temporary,
            &createInformation,
            message) ||
        createInformation != FILE_CREATED)
    {
        return false;
    }
    bool renamed = false;
    const auto removeTemporary =
        [&temporary, &renamed]()
        {
            if (!temporary.valid() || renamed)
                return;
            FILE_DISPOSITION_INFO disposition = {};
            disposition.DeleteFile = TRUE;
            SetFileInformationByHandle(
                temporary.get(),
                FileDispositionInfo,
                &disposition,
                sizeof(disposition));
        };
    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                AfterAtomicTemporaryOpen,
            path) ||
        injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                BeforeAtomicWrite,
            path))
    {
        message = QStringLiteral(
            "Injected atomic write failure after temporary creation");
        removeTemporary();
        return false;
    }
    qsizetype offset = 0;
    while (offset < bytes.size())
    {
        const DWORD requested =
            static_cast<DWORD>(
                std::min<qsizetype>(
                    bytes.size() - offset,
                    1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(
                temporary.get(),
                bytes.constData() + offset,
                requested,
                &written,
                nullptr) ||
            written != requested)
        {
            message = QStringLiteral(
                "Cannot write an anchored atomic output "
                "(system error %1)")
                .arg(GetLastError());
            removeTemporary();
            return false;
        }
        offset += static_cast<qsizetype>(
            written);
    }
    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                BeforeAtomicFlush,
            path) ||
        !FlushFileBuffers(temporary.get()))
    {
        message = QStringLiteral(
            "Cannot flush an anchored atomic output "
            "(system error %1)")
            .arg(GetLastError());
        removeTemporary();
        return false;
    }
    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                BeforeAtomicRename,
            path))
    {
        message = QStringLiteral(
            "Injected atomic rename failure");
        removeTemporary();
        return false;
    }
    const std::wstring finalName =
        fileName.toStdWString();
    const std::size_t nameBytes =
        finalName.size() * sizeof(wchar_t);
    if (nameBytes >
        (std::numeric_limits<DWORD>::max)())
    {
        message = QStringLiteral(
            "Atomic output name is too long");
        removeTemporary();
        return false;
    }
    const std::size_t informationBytes =
        sizeof(FILE_RENAME_INFO) + nameBytes;
    std::vector<quint64> storage(
        (informationBytes +
         sizeof(quint64) - 1) /
            sizeof(quint64),
        0);
    auto* renameInformation =
        reinterpret_cast<FILE_RENAME_INFO*>(
            storage.data());
    renameInformation->ReplaceIfExists = FALSE;
    renameInformation->RootDirectory = parent;
    renameInformation->FileNameLength =
        static_cast<DWORD>(nameBytes);
    std::memcpy(
        renameInformation->FileName,
        finalName.data(),
        nameBytes);
    IO_STATUS_BLOCK ioStatus = {};
    const NtSetInformationFileFunction setInformation =
        nativeNtSetInformationFile();
    const NTSTATUS status =
        setInformation != nullptr
        ? setInformation(
              temporary.get(),
              &ioStatus,
              renameInformation,
              static_cast<ULONG>(
                  informationBytes),
              static_cast<
                  FILE_INFORMATION_CLASS>(10))
        : static_cast<NTSTATUS>(-1);
    if (status < 0)
    {
        message = QStringLiteral(
            "Anchored atomic rename failed with status 0x%1")
            .arg(
                static_cast<quint32>(status),
                8,
                16,
                QLatin1Char('0'));
        removeTemporary();
        return false;
    }
    renamed = true;
#else
    const QByteArray temporaryNative =
        QFile::encodeName(temporaryName);
    const QByteArray finalNative =
        QFile::encodeName(fileName);
    const int descriptor = ::openat(
        parent,
        temporaryNative.constData(),
        O_WRONLY |
            O_CREAT |
            O_EXCL |
            O_CLOEXEC |
            O_NOFOLLOW,
        0600);
    if (descriptor < 0)
    {
        message = QStringLiteral(
            "Cannot create an anchored atomic temporary file "
            "(system error %1)")
            .arg(errno);
        return false;
    }
    NativeHandle temporary(descriptor);
    bool renamed = false;
    const auto removeTemporary =
        [parent,
         &temporaryNative,
         &renamed]()
        {
            if (!renamed)
            {
                ::unlinkat(
                    parent,
                    temporaryNative.constData(),
                    0);
            }
        };
    if (::fchmod(
            temporary.get(),
            0600) != 0)
    {
        message = QStringLiteral(
            "Cannot apply private permissions to an anchored atomic temporary file "
            "(system error %1)")
            .arg(errno);
        removeTemporary();
        return false;
    }
    NativeMetadata temporaryMetadata;
    QString metadataMessage;
    if (!metadataFromNativeHandle(
            temporary.get(),
            temporaryMetadata,
            metadataMessage) ||
        temporaryMetadata.kind !=
            NativeNodeKind::RegularFile ||
        temporaryMetadata.linkCount != 1 ||
        (temporaryMetadata.modeOrAttributes &
         0777) != 0600)
    {
        message = QStringLiteral(
            "Atomic temporary file is not private and regular");
        removeTemporary();
        return false;
    }
    const auto temporaryIsPrivateAndCurrent =
        [&](const QByteArray& leafName)
        {
            NativeMetadata heldMetadata;
            QString heldMessage;
            if (!metadataFromNativeHandle(
                    temporary.get(),
                    heldMetadata,
                    heldMessage) ||
                heldMetadata.kind !=
                    NativeNodeKind::RegularFile ||
                heldMetadata.linkCount != 1 ||
                (heldMetadata.modeOrAttributes &
                 0777) != 0600 ||
                !sameIdentity(
                    temporaryMetadata,
                    heldMetadata))
            {
                return false;
            }

            struct stat namedInformation = {};
            if (::fstatat(
                    parent,
                    leafName.constData(),
                    &namedInformation,
                    AT_SYMLINK_NOFOLLOW) != 0)
            {
                return false;
            }
            NativeMetadata namedMetadata;
            QString namedMessage;
            return metadataFromPosixStat(
                       namedInformation,
                       namedMetadata,
                       namedMessage) &&
                namedMetadata.kind ==
                    NativeNodeKind::RegularFile &&
                namedMetadata.linkCount == 1 &&
                sameIdentity(
                    temporaryMetadata,
                    namedMetadata);
        };
    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                AfterAtomicTemporaryOpen,
            path) ||
        injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                BeforeAtomicWrite,
            path))
    {
        message = QStringLiteral(
            "Injected atomic write failure after temporary creation");
        removeTemporary();
        return false;
    }
    if (!temporaryIsPrivateAndCurrent(
            temporaryNative))
    {
        message = QStringLiteral(
            "Atomic temporary file identity or link count changed before write");
        removeTemporary();
        return false;
    }
    qsizetype offset = 0;
    while (offset < bytes.size())
    {
        const ssize_t written = ::write(
            temporary.get(),
            bytes.constData() + offset,
            static_cast<std::size_t>(
                bytes.size() - offset));
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            message = QStringLiteral(
                "Cannot write an anchored atomic output "
                "(system error %1)")
                .arg(errno);
            removeTemporary();
            return false;
        }
        if (written == 0)
        {
            message = QStringLiteral(
                "Atomic output write made no progress");
            removeTemporary();
            return false;
        }
        offset += static_cast<qsizetype>(
            written);
    }
    if (!temporaryIsPrivateAndCurrent(
            temporaryNative))
    {
        message = QStringLiteral(
            "Atomic temporary file identity or link count changed during write");
        removeTemporary();
        return false;
    }
    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                BeforeAtomicFlush,
            path) ||
        ::fsync(temporary.get()) != 0)
    {
        message = QStringLiteral(
            "Cannot flush an anchored atomic output "
            "(system error %1)")
            .arg(errno);
        removeTemporary();
        return false;
    }
    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                BeforeAtomicRename,
            path))
    {
        message = QStringLiteral(
            "Injected atomic rename failure");
        removeTemporary();
        return false;
    }
    if (!temporaryIsPrivateAndCurrent(
            temporaryNative))
    {
        message = QStringLiteral(
            "Atomic temporary file identity or link count changed before rename");
        removeTemporary();
        return false;
    }
    struct stat finalInformation = {};
    if (::fstatat(
            parent,
            finalNative.constData(),
            &finalInformation,
            AT_SYMLINK_NOFOLLOW) == 0 ||
        errno != ENOENT)
    {
        message = QStringLiteral(
            "Atomic output target already exists or cannot be inspected");
        removeTemporary();
        return false;
    }
    if (::renameat(
            parent,
            temporaryNative.constData(),
            parent,
            finalNative.constData()) != 0)
    {
        message = QStringLiteral(
            "Cannot atomically rename an anchored output "
            "(system error %1)")
            .arg(errno);
        removeTemporary();
        return false;
    }
    renamed = true;
    if (!temporaryIsPrivateAndCurrent(
            finalNative))
    {
        message = QStringLiteral(
            "Atomic output identity or link count changed after rename");
        return false;
    }
    if (::fsync(parent) != 0)
    {
        message = QStringLiteral(
            "Cannot flush the anchored atomic output directory "
            "(system error %1)")
            .arg(errno);
        return false;
    }
#endif
    return true;
}

DesktopRunSessionPaths pathsForSession(
    const QString& sessionRoot)
{
    DesktopRunSessionPaths paths;
    paths.sessionRoot = sessionRoot;
    QDir sessionDirectory(sessionRoot);
    paths.overlayRoot =
        sessionDirectory.filePath(OverlayDirectoryName);
    paths.isolatedSaveRoot =
        sessionDirectory.filePath(SaveDirectoryName);
    paths.applicationStateRoot =
        sessionDirectory.filePath(
            ApplicationStateDirectoryName);
    paths.diagnosticsRoot =
        sessionDirectory.filePath(
            DiagnosticsDirectoryName);
    paths.diagnosticsPath =
        QDir(paths.diagnosticsRoot).filePath(
            DiagnosticsFileName);
    paths.logPath =
        QDir(paths.diagnosticsRoot).filePath(
            LogFileName);
    paths.runtimeTracePath =
        QDir(paths.diagnosticsRoot).filePath(
            RuntimeTraceFileName);
    paths.markerPath =
        sessionDirectory.filePath(MarkerFileName);
    paths.resourceRoutingContractPath =
        sessionDirectory.filePath(
            ResourceRoutingContractFileName);
    paths.descriptorPath =
        sessionDirectory.filePath(
            DescriptorFileName);
    return paths;
}

fs::path descriptorHostPath(const QString& path)
{
    fs::path nativePath;
    if (!hostPath(path, nativePath))
        return {};
    return nativePath;
}

bool exactStdString(
    const QString& text,
    std::string& output)
{
    const QByteArray utf8 = text.toUtf8();
    if (QString::fromUtf8(
            utf8.constData(),
            utf8.size()) != text)
    {
        return false;
    }
    output.assign(
        utf8.constData(),
        static_cast<std::size_t>(
            utf8.size()));
    return true;
}

bool descriptorTemplateForPreparedLaunch(
    const PreparedSavedSceneLaunch& preparedLaunch,
    EditorRun::Descriptor& descriptor)
{
    EditorRun::Descriptor candidate;
    if (!hostPath(
            preparedLaunch.assetsCollectionRoot,
            candidate.assetsCollectionRoot) ||
        !exactStdString(
            preparedLaunch.
                canonicalActiveResourcePackId,
            candidate.activeResourcePackId) ||
        !exactStdString(
            preparedLaunch.
                activeResourcePackEntryKey,
            candidate.activeResourcePackEntryKey) ||
        !exactStdString(
            preparedLaunch.scene.id,
            candidate.target.sceneId) ||
        !exactStdString(
            preparedLaunch.scene.name,
            candidate.target.sceneName) ||
        !exactStdString(
            preparedLaunch.scene.mapPath,
            candidate.target.mapPath) ||
        !exactStdString(
            preparedLaunch.scene.npcPath,
            candidate.target.npcPath) ||
        !exactStdString(
            preparedLaunch.scene.objectPath,
            candidate.target.objectPath) ||
        !exactStdString(
            preparedLaunch.scene.entryScriptPath,
            candidate.target.entryScriptPath))
    {
        return false;
    }
    candidate.target.playerX =
        static_cast<std::int32_t>(
            preparedLaunch.scene.playerPosition.x());
    candidate.target.playerY =
        static_cast<std::int32_t>(
            preparedLaunch.scene.playerPosition.y());
    candidate.target.kind =
        preparedLaunch.targetKind;
    for (auto variable =
             preparedLaunch.scene.integerVariables.cbegin();
         variable !=
             preparedLaunch.scene.integerVariables.cend();
         ++variable)
    {
        std::string name;
        if (!exactStdString(
                variable.key(), name))
        {
            return false;
        }
        candidate.target.integerVariables.emplace(
            std::move(name),
            static_cast<std::int32_t>(
                variable.value()));
    }
    descriptor = std::move(candidate);
    return true;
}

bool descriptorBusinessFieldsMatch(
    const EditorRun::Descriptor& descriptor,
    const EditorRun::Descriptor& expected)
{
    return descriptor.activeResourcePackId ==
            expected.activeResourcePackId &&
        descriptor.activeResourcePackEntryKey ==
            expected.activeResourcePackEntryKey &&
        descriptor.target.kind ==
            expected.target.kind &&
        descriptor.target.sceneId ==
            expected.target.sceneId &&
        descriptor.target.sceneName ==
            expected.target.sceneName &&
        descriptor.target.mapPath ==
            expected.target.mapPath &&
        descriptor.target.npcPath ==
            expected.target.npcPath &&
        descriptor.target.objectPath ==
            expected.target.objectPath &&
        descriptor.target.entryScriptPath ==
            expected.target.entryScriptPath &&
        descriptor.target.playerX ==
            expected.target.playerX &&
        descriptor.target.playerY ==
            expected.target.playerY &&
        descriptor.target.integerVariables ==
            expected.target.integerVariables;
}

EditorRun::Descriptor materializeDescriptor(
    const EditorRun::Descriptor& descriptorTemplate,
    const QString& sessionId,
    const DesktopRunSessionPaths& paths)
{
    EditorRun::Descriptor descriptor =
        descriptorTemplate;
    descriptor.sessionId =
        sessionId.toStdString();
    descriptor.overlayRoot =
        descriptorHostPath(paths.overlayRoot);
    descriptor.isolatedSaveRoot =
        descriptorHostPath(paths.isolatedSaveRoot);
    descriptor.applicationStateRoot =
        descriptorHostPath(paths.applicationStateRoot);
    descriptor.diagnosticsPath =
        descriptorHostPath(paths.diagnosticsPath);
    descriptor.logPath =
        descriptorHostPath(paths.logPath);
    return descriptor;
}

bool anchoredChildExists(
    NativeHandle::Value parent,
    const QString& name)
{
#ifdef Q_OS_WIN
    NativeHandle opened;
    QString message;
    return ntOpenRelative(
        parent,
        name.toStdWString(),
        FILE_READ_ATTRIBUTES |
            SYNCHRONIZE,
        FILE_OPEN,
        0,
        FILE_ATTRIBUTE_NORMAL,
        opened,
        nullptr,
        message);
#else
    const QByteArray nativeName =
        QFile::encodeName(name);
    struct stat information = {};
    return ::fstatat(
               parent,
               nativeName.constData(),
               &information,
               AT_SYMLINK_NOFOLLOW) == 0;
#endif
}

bool createAnchoredDirectory(
    NativeHandle::Value parent,
    const QString& name,
    const QString& displayPath,
    DirectoryAnchor& created,
    bool& materialized,
    QString& message)
{
    materialized = false;
    QByteArray nameUtf8;
    if (!pathComponentIsSafe(name, nameUtf8))
    {
        message = QStringLiteral(
            "Directory name is not a safe path component");
        return false;
    }
#ifdef Q_OS_WIN
    ULONG_PTR information = 0;
    if (!ntOpenRelative(
            parent,
            name.toStdWString(),
            FILE_LIST_DIRECTORY |
                FILE_ADD_FILE |
                FILE_ADD_SUBDIRECTORY |
                FILE_READ_ATTRIBUTES |
                FILE_WRITE_ATTRIBUTES |
                READ_CONTROL |
                FILE_DELETE_CHILD |
                DELETE |
                SYNCHRONIZE,
            FILE_CREATE,
            FILE_DIRECTORY_FILE,
            FILE_ATTRIBUTE_DIRECTORY,
            created.handle,
            &information,
            message))
    {
        return false;
    }
    if (information != FILE_CREATED)
    {
        created.handle.reset();
        message = QStringLiteral(
            "Anchored directory creation did not create a new identity");
        return false;
    }
    materialized = true;
#else
    const QByteArray nativeName =
        QFile::encodeName(name);
    if (::mkdirat(
            parent,
            nativeName.constData(),
            0700) != 0)
    {
        message = QStringLiteral(
            "Cannot create an anchored directory "
            "(system error %1)")
            .arg(errno);
        return false;
    }
    materialized = true;
    if (::fchmodat(
            parent,
            nativeName.constData(),
            0700,
            0) != 0)
    {
        message = QStringLiteral(
            "Cannot apply private permissions to a newly created anchored directory "
            "(system error %1)")
            .arg(errno);
        return false;
    }
    const int descriptor = ::openat(
        parent,
        nativeName.constData(),
        O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW);
    if (descriptor < 0)
    {
        message = QStringLiteral(
            "Cannot open a newly created anchored directory "
            "(system error %1)")
            .arg(errno);
        return false;
    }
    created.handle.reset(descriptor);
#endif
    if (!metadataFromNativeHandle(
            created.handle.get(),
            created.metadata,
            message) ||
        created.metadata.kind !=
            NativeNodeKind::Directory)
    {
        return false;
    }
#ifdef Q_OS_WIN
    if (!trustedWindowsDirectoryPermissions(
            created.handle.get(), message))
#else
    if (!trustedPosixDirectoryPermissions(
            created.handle.get(), message))
#endif
    {
        return false;
    }
    created.canonicalPath =
        QDir::cleanPath(displayPath);
    if (!exactUtf8(
            created.canonicalPath,
            created.canonicalPathUtf8) ||
        !anchoredPathStillHasIdentity(
            created.canonicalPath,
            created.metadata))
    {
        message = QStringLiteral(
            "New directory path does not name its anchored identity");
        return false;
    }
    return true;
}

bool createLeafDirectory(
    DirectoryAnchor& sessionRoot,
    const QString& leafName,
    const QString& leafPath,
    DirectoryAnchor& created,
    DesktopRunSessionWorkspaceResult& result)
{
    QString message;
    bool materialized = false;
    if (!createAnchoredDirectory(
            sessionRoot.handle.get(),
            leafName,
            leafPath,
            created,
            materialized,
            message))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                SessionLeafCreationFailed,
            leafPath,
            message.isEmpty()
            ? QStringLiteral(
                  "Cannot create an editor-run session leaf directory")
            : message);
        return false;
    }
    return true;
}

enum class RoutingStatus
{
    Current,
    SessionChanged
};

struct RoutingCheck
{
    RoutingStatus status = RoutingStatus::Current;
    QString problemPath;
};

RoutingCheck checkCurrentRouting(
    const DirectoryAnchor& sessionsBase,
    const DirectoryAnchor& sessionRoot,
    const std::array<DirectoryAnchor, 4>& leaves)
{
    if (!anchoredPathStillHasIdentity(
            sessionsBase.canonicalPath,
            sessionsBase.metadata) ||
        !anchoredPathStillHasIdentity(
            sessionRoot.canonicalPath,
            sessionRoot.metadata))
    {
        return {
            RoutingStatus::SessionChanged,
            sessionRoot.canonicalPath
        };
    }
    for (const DirectoryAnchor& leaf : leaves)
    {
        if (!anchoredPathStillHasIdentity(
                leaf.canonicalPath,
                leaf.metadata) ||
            sameIdentity(
                leaf.metadata,
                sessionsBase.metadata) ||
            sameIdentity(
                leaf.metadata,
                sessionRoot.metadata))
        {
            return {
                RoutingStatus::SessionChanged,
                leaf.canonicalPath
            };
        }
    }
    for (std::size_t left = 0;
         left < leaves.size();
         ++left)
    {
        for (std::size_t right = left + 1;
             right < leaves.size();
             ++right)
        {
            if (sameIdentity(
                leaves[left].metadata,
                leaves[right].metadata))
            {
                return {
                    RoutingStatus::SessionChanged,
                    leaves[right].canonicalPath
                };
            }
        }
    }
    return {};
}

bool failIfRoutingChanged(
    DesktopRunSessionWorkspaceResult& result,
    const DirectoryAnchor& sessionsBase,
    const DirectoryAnchor& sessionRoot,
    const std::array<DirectoryAnchor, 4>& leaves,
    const QString& operation)
{
    const RoutingCheck routing = checkCurrentRouting(
        sessionsBase,
        sessionRoot,
        leaves);
    if (routing.status == RoutingStatus::Current)
        return false;

    fail(
        result,
        DesktopRunSessionWorkspaceError::
            SessionRoutingChanged,
        routing.problemPath,
        operation +
            QStringLiteral(
                ": the private session route changed"));
    result.partialFailureRoot.clear();
    result.cleanupWorkspace.reset();
    return true;
}

struct ValidatedOverlayNode
{
    QString name;
    QByteArray nameUtf8;
    const PreparedDesktopRunOverlayFile* file =
        nullptr;
    std::vector<ValidatedOverlayNode> children;
};

struct ValidatedPreparedOverlay
{
    ValidatedOverlayNode root;
    QVector<DesktopRunTraceOverlayOrigin> origins;
    quint64 totalBytes = 0;
    quint64 entryCount = 0;
};

ValidatedOverlayNode* overlayChild(
    ValidatedOverlayNode& parent,
    const QString& name,
    Qt::CaseSensitivity caseSensitivity)
{
    const auto found =
        std::find_if(
            parent.children.begin(),
            parent.children.end(),
            [&name, caseSensitivity](
                const ValidatedOverlayNode& child)
            {
                return child.name.compare(
                           name,
                           caseSensitivity) == 0;
            });
    return found == parent.children.end()
        ? nullptr
        : &*found;
}

void sortValidatedOverlayTree(
    ValidatedOverlayNode& node)
{
    std::sort(
        node.children.begin(),
        node.children.end(),
        [](const ValidatedOverlayNode& left,
           const ValidatedOverlayNode& right)
        {
            return left.nameUtf8 <
                right.nameUtf8;
        });
    for (ValidatedOverlayNode& child :
         node.children)
    {
        sortValidatedOverlayTree(child);
    }
}

bool validatePreparedOverlay(
    const PreparedSavedSceneLaunch& preparedLaunch,
    const DesktopRunSessionWorkspaceLimits& limits,
    ValidatedPreparedOverlay& validated,
    DesktopRunSessionWorkspaceResult& result)
{
    validated = {};
    if (preparedLaunch.overlayFiles.size() >
        MaximumDesktopRunOverlayFileCount)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                PreparedOverlayLimitExceeded,
            {},
            QStringLiteral(
                "Prepared sparse overlay file count exceeds the preparation limit"));
        return false;
    }

    const Qt::CaseSensitivity caseSensitivity =
        EditorAssetPath::caseSensitivity(
            preparedLaunch.activeContentRoot);
    quint64 retainedPathBytes = 0;
    for (const PreparedDesktopRunOverlayFile& file :
         preparedLaunch.overlayFiles)
    {
        if (file.contentRootOrdinal >=
            static_cast<std::size_t>(
                preparedLaunch.orderedContentRoots.size()))
        {
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    PreparedOverlayInvalid,
                file.virtualPath,
                QStringLiteral(
                    "Prepared sparse overlay does not name an existing logical formal content-root ordinal"));
            return false;
        }
        const ResourceContentRoot& sourceRoot =
            preparedLaunch.orderedContentRoots.at(
                static_cast<qsizetype>(
                    file.contentRootOrdinal));
        const QString sourceRootKind =
            traceRootKindName(sourceRoot.kind);
        const QString sourceRootPath =
            QDir::fromNativeSeparators(
                normalizedAbsolutePath(
                    sourceRoot.rootPath));
        QByteArray sourceRootPathUtf8;
        QByteArray resourcePackIdUtf8;
        if (sourceRootKind.isEmpty() ||
            sourceRoot.rootPath.isEmpty() ||
            !QDir::isAbsolutePath(
                sourceRoot.rootPath) ||
            sourceRootPath.isEmpty() ||
            !QDir::isAbsolutePath(
                sourceRootPath) ||
            QDir::cleanPath(sourceRootPath) !=
                sourceRootPath ||
            !exactUtf8(
                sourceRootPath,
                sourceRootPathUtf8) ||
            sourceRootPathUtf8.size() >
                limits.maximumPathBytes ||
            !exactUtf8(
                sourceRoot.id,
                resourcePackIdUtf8) ||
            resourcePackIdUtf8.size() >
                limits.maximumPathBytes)
        {
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    PreparedOverlayInvalid,
                sourceRoot.rootPath,
                QStringLiteral(
                    "Prepared sparse overlay source root does not have bounded stable logical provenance"));
            return false;
        }
        QByteArray pathUtf8;
        if (!exactUtf8(
                file.virtualPath,
                pathUtf8))
        {
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    PreparedOverlayInvalid,
                file.virtualPath,
                QStringLiteral(
                    "Prepared sparse overlay path is not exact UTF-8"));
            return false;
        }
        const std::string pathBytes(
            pathUtf8.constData(),
            static_cast<std::size_t>(
                pathUtf8.size()));
        const QStringList components =
            file.virtualPath.split(
                QChar('/'),
                Qt::KeepEmptyParts);
        if (file.virtualPath.isEmpty() ||
            pathUtf8.size() >
                limits.maximumPathBytes ||
            !ResourcePathSafety::
                isSafeVirtualResourcePath(
                    pathBytes) ||
            components.isEmpty() ||
            components.size() - 1 >
                limits.maximumDirectoryDepth)
        {
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    PreparedOverlayInvalid,
                file.virtualPath,
                QStringLiteral(
                    "Prepared sparse overlay path is unsafe or outside workspace path limits"));
            return false;
        }
        for (const QString& component :
             components)
        {
            QByteArray componentUtf8;
            if (!pathComponentIsSafe(
                    component,
                    componentUtf8))
            {
                fail(
                    result,
                    DesktopRunSessionWorkspaceError::
                        PreparedOverlayInvalid,
                    file.virtualPath,
                    QStringLiteral(
                        "Prepared sparse overlay contains an unsafe path component"));
                return false;
            }
        }

        if (file.sha256.size() !=
                QCryptographicHash::hashLength(
                    QCryptographicHash::Sha256) ||
            QCryptographicHash::hash(
                file.bytes,
                QCryptographicHash::Sha256) !=
                    file.sha256)
        {
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    PreparedOverlayInvalid,
                file.virtualPath,
                QStringLiteral(
                    "Prepared sparse overlay bytes do not match their SHA-256"));
            return false;
        }

        const quint64 byteCount =
            static_cast<quint64>(
                file.bytes.size());
        if (byteCount >
                MaximumDesktopRunOverlayBytes ||
            validated.totalBytes >
                MaximumDesktopRunOverlayBytes -
                    byteCount)
        {
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    PreparedOverlayLimitExceeded,
                file.virtualPath,
                QStringLiteral(
                    "Prepared sparse overlay bytes exceed the preparation limit"));
            return false;
        }
        validated.totalBytes += byteCount;

        const quint64 currentPathBytes =
            static_cast<quint64>(
                pathUtf8.size()) +
            static_cast<quint64>(
                sourceRootPathUtf8.size()) +
            static_cast<quint64>(
                resourcePackIdUtf8.size()) +
            static_cast<quint64>(
                sourceRootKind.size());
        if (currentPathBytes >
                limits.maximumWorkingBytes ||
            retainedPathBytes >
                limits.maximumWorkingBytes -
                    currentPathBytes)
        {
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    PreparedOverlayLimitExceeded,
                file.virtualPath,
                QStringLiteral(
                    "Prepared sparse overlay path metadata exceeds the workspace working-memory limit"));
            return false;
        }
        retainedPathBytes += currentPathBytes;

        ValidatedOverlayNode* parent =
            &validated.root;
        for (qsizetype index = 0;
             index < components.size();
             ++index)
        {
            const QString& component =
                components.at(index);
            const bool finalComponent =
                index + 1 ==
                components.size();
            ValidatedOverlayNode* child =
                overlayChild(
                    *parent,
                    component,
                    caseSensitivity);
            if (child == nullptr)
            {
                if (static_cast<quint64>(
                        parent->children.size()) >=
                    limits.
                        maximumDirectoryEntryCount)
                {
                    fail(
                        result,
                        DesktopRunSessionWorkspaceError::
                            PreparedOverlayLimitExceeded,
                        file.virtualPath,
                        QStringLiteral(
                            "Prepared sparse overlay directory width exceeds the workspace limit"));
                    return false;
                }
                QByteArray componentUtf8;
                exactUtf8(
                    component,
                    componentUtf8);
                ValidatedOverlayNode created;
                created.name = component;
                created.nameUtf8 =
                    std::move(componentUtf8);
                parent->children.push_back(
                    std::move(created));
                child =
                    &parent->children.back();
                ++validated.entryCount;
                if (validated.entryCount >
                    limits.maximumEntryCount)
                {
                    fail(
                        result,
                        DesktopRunSessionWorkspaceError::
                            PreparedOverlayLimitExceeded,
                        file.virtualPath,
                        QStringLiteral(
                            "Prepared sparse overlay tree exceeds the workspace entry limit"));
                    return false;
                }
            }
            if (finalComponent)
            {
                if (child->file != nullptr ||
                    !child->children.empty())
                {
                    fail(
                        result,
                        DesktopRunSessionWorkspaceError::
                            PreparedOverlayPathCollision,
                        file.virtualPath,
                        QStringLiteral(
                            "Prepared sparse overlay contains a duplicate or file-directory path collision"));
                    return false;
                }
                child->file = &file;
                DesktopRunTraceOverlayOrigin origin;
                origin.virtualPath =
                    file.virtualPath;
                origin.rootOrdinal =
                    static_cast<quint64>(
                        file.contentRootOrdinal);
                origin.rootKind =
                    sourceRootKind;
                origin.resourcePackId =
                    sourceRoot.id;
                origin.rootPath =
                    sourceRootPath;
                validated.origins.append(
                    std::move(origin));
            }
            else
            {
                if (child->file != nullptr)
                {
                    fail(
                        result,
                        DesktopRunSessionWorkspaceError::
                            PreparedOverlayPathCollision,
                        file.virtualPath,
                        QStringLiteral(
                            "Prepared sparse overlay file path collides with a required parent directory"));
                    return false;
                }
                parent = child;
            }
        }
    }
    sortValidatedOverlayTree(
        validated.root);
    std::sort(
        validated.origins.begin(),
        validated.origins.end(),
        [](const DesktopRunTraceOverlayOrigin& left,
           const DesktopRunTraceOverlayOrigin& right)
        {
            return left.virtualPath.toUtf8() <
                right.virtualPath.toUtf8();
        });
    return true;
}

bool anchoredChildHasIdentity(
    NativeHandle::Value parent,
    const QString& name,
    const NativeMetadata& expected)
{
#ifdef Q_OS_WIN
    NativeHandle reopened;
    QString message;
    NativeMetadata current;
    return ntOpenRelative(
               parent,
               name.toStdWString(),
               FILE_READ_ATTRIBUTES |
                   SYNCHRONIZE,
               FILE_OPEN,
               0,
               FILE_ATTRIBUTE_NORMAL,
               reopened,
               nullptr,
               message) &&
        metadataFromNativeHandle(
            reopened.get(),
            current,
            message) &&
        sameIdentity(expected, current);
#else
    const QByteArray nativeName =
        QFile::encodeName(name);
    struct stat information = {};
    NativeMetadata current;
    QString message;
    return ::fstatat(
               parent,
               nativeName.constData(),
               &information,
               AT_SYMLINK_NOFOLLOW) == 0 &&
        metadataFromPosixStat(
            information,
            current,
            message) &&
        sameIdentity(expected, current);
#endif
}

bool flushAnchoredOverlayDirectory(
    NativeHandle::Value directory,
    QString& message)
{
#ifdef Q_OS_WIN
    // Every overlay file is flushed before publication. Windows does not
    // provide a portable directory-fsync contract for ordinary directory
    // handles; the final rename remains one anchored NT metadata operation.
    Q_UNUSED(directory);
    Q_UNUSED(message);
    return true;
#else
    if (::fsync(directory) == 0)
        return true;
    message = QStringLiteral(
        "Cannot flush a private sparse-overlay directory (system error %1)")
        .arg(errno);
    return false;
#endif
}

bool writeAnchoredOverlayFile(
    NativeHandle::Value parent,
    quint64 expectedDevice,
    const PreparedDesktopRunOverlayFile& file,
    const QString& displayPath,
    QString& message)
{
    const QString leaf =
        file.virtualPath.section(
            QChar('/'), -1);
    QByteArray leafUtf8;
    if (!pathComponentIsSafe(
            leaf,
            leafUtf8))
    {
        message = QStringLiteral(
            "Sparse-overlay file name is not a safe path component");
        return false;
    }
    NativeHandle output;
    NativeMetadata initialMetadata;
#ifdef Q_OS_WIN
    ULONG_PTR information = 0;
    if (!ntOpenRelative(
            parent,
            leaf.toStdWString(),
            FILE_READ_DATA |
                FILE_WRITE_DATA |
                FILE_READ_ATTRIBUTES |
                FILE_WRITE_ATTRIBUTES |
                DELETE |
                SYNCHRONIZE,
            FILE_CREATE,
            FILE_NON_DIRECTORY_FILE,
            FILE_ATTRIBUTE_NORMAL,
            output,
            &information,
            message) ||
        information != FILE_CREATED ||
        !metadataFromNativeHandle(
            output.get(),
            initialMetadata,
            message))
    {
        return false;
    }
#else
    const int descriptor = ::openat(
        parent,
        leafUtf8.constData(),
        O_RDWR |
            O_CREAT |
            O_EXCL |
            O_CLOEXEC |
            O_NOFOLLOW,
        0600);
    if (descriptor < 0)
    {
        message = QStringLiteral(
            "Cannot create an anchored sparse-overlay file (system error %1)")
            .arg(errno);
        return false;
    }
    output.reset(descriptor);
    if (::fchmod(
            output.get(),
            0600) != 0 ||
        !metadataFromNativeHandle(
            output.get(),
            initialMetadata,
            message))
    {
        if (message.isEmpty())
        {
            message = QStringLiteral(
                "Cannot apply private sparse-overlay file permissions (system error %1)")
                .arg(errno);
        }
        return false;
    }
#endif
    if (initialMetadata.kind !=
            NativeNodeKind::RegularFile ||
        initialMetadata.device !=
            expectedDevice ||
        initialMetadata.linkCount != 1)
    {
        message = QStringLiteral(
            "Created sparse-overlay file is linked, reparsed, hard-linked, or routed to another device");
        return false;
    }

    qsizetype offset = 0;
    bool faultChecked = false;
    while (offset < file.bytes.size())
    {
#ifdef Q_OS_WIN
        const DWORD requested =
            static_cast<DWORD>(
                std::min<qsizetype>(
                    file.bytes.size() - offset,
                    HashBufferBytes));
        DWORD written = 0;
        if (!WriteFile(
                output.get(),
                file.bytes.constData() + offset,
                requested,
                &written,
                nullptr) ||
            written != requested)
        {
            message = QStringLiteral(
                "Cannot write an anchored sparse-overlay file (system error %1)")
                .arg(GetLastError());
            return false;
        }
#else
        const ssize_t written = ::write(
            output.get(),
            file.bytes.constData() + offset,
            static_cast<std::size_t>(
                file.bytes.size() - offset));
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            message = QStringLiteral(
                "Cannot write an anchored sparse-overlay file (system error %1)")
                .arg(errno);
            return false;
        }
        if (written == 0)
        {
            message = QStringLiteral(
                "Sparse-overlay write made no progress");
            return false;
        }
#endif
        offset +=
            static_cast<qsizetype>(written);
        if (!faultChecked)
        {
            faultChecked = true;
            if (injectFailure(
                    DesktopRunSessionWorkspaceFaultPoint::
                        DuringOverlayFileWrite,
                    displayPath))
            {
                message = QStringLiteral(
                    "Injected sparse-overlay file write failure");
                return false;
            }
        }
    }
    if (!faultChecked &&
        injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                DuringOverlayFileWrite,
            displayPath))
    {
        message = QStringLiteral(
            "Injected sparse-overlay file write failure");
        return false;
    }

#ifdef Q_OS_WIN
    if (!FlushFileBuffers(output.get()))
    {
        message = QStringLiteral(
            "Cannot flush an anchored sparse-overlay file (system error %1)")
            .arg(GetLastError());
        return false;
    }
#else
    if (::fsync(output.get()) != 0)
    {
        message = QStringLiteral(
            "Cannot flush an anchored sparse-overlay file (system error %1)")
            .arg(errno);
        return false;
    }
#endif

    NativeMetadata finalMetadata;
    if (!metadataFromNativeHandle(
            output.get(),
            finalMetadata,
            message) ||
        !sameIdentity(
            initialMetadata,
            finalMetadata) ||
        finalMetadata.kind !=
            NativeNodeKind::RegularFile ||
        finalMetadata.device !=
            expectedDevice ||
        finalMetadata.linkCount != 1 ||
        finalMetadata.size !=
            static_cast<quint64>(
                file.bytes.size()) ||
        !anchoredChildHasIdentity(
            parent,
            leaf,
            finalMetadata))
    {
        message = QStringLiteral(
            "Sparse-overlay file identity, link count, route, or size changed while writing");
        return false;
    }
    if (!flushAnchoredOverlayDirectory(
            parent,
            message))
    {
        return false;
    }
    return true;
}

bool materializeOverlayTree(
    DirectoryAnchor& directory,
    quint64 expectedDevice,
    const ValidatedOverlayNode& expected,
    QString& problemPath,
    QString& message)
{
    for (const ValidatedOverlayNode& child :
         expected.children)
    {
        const QString childPath =
            QDir(directory.canonicalPath).
                filePath(child.name);
        if (child.file != nullptr)
        {
            if (!writeAnchoredOverlayFile(
                    directory.handle.get(),
                    expectedDevice,
                    *child.file,
                    childPath,
                    message))
            {
                problemPath = childPath;
                return false;
            }
            continue;
        }

        DirectoryAnchor childDirectory;
        bool materialized = false;
        if (!createAnchoredDirectory(
                directory.handle.get(),
                child.name,
                childPath,
                childDirectory,
                materialized,
                message) ||
            !materialized ||
            childDirectory.metadata.device !=
                expectedDevice ||
            !materializeOverlayTree(
                childDirectory,
                expectedDevice,
                child,
                problemPath,
                message) ||
            !anchoredChildHasIdentity(
                directory.handle.get(),
                child.name,
                childDirectory.metadata) ||
            !anchoredPathStillHasIdentity(
                childDirectory.canonicalPath,
                childDirectory.metadata) ||
            !flushAnchoredOverlayDirectory(
                childDirectory.handle.get(),
                message))
        {
            if (problemPath.isEmpty())
                problemPath = childPath;
            if (message.isEmpty())
            {
                message = QStringLiteral(
                    "Cannot create or retain an anchored sparse-overlay directory");
            }
            return false;
        }
    }
    if (!anchoredPathStillHasIdentity(
            directory.canonicalPath,
            directory.metadata) ||
        !flushAnchoredOverlayDirectory(
            directory.handle.get(),
            message))
    {
        problemPath = directory.canonicalPath;
        if (message.isEmpty())
        {
            message = QStringLiteral(
                "Sparse-overlay directory route changed while staging files");
        }
        return false;
    }
    return true;
}

bool publishOverlayDirectory(
    DirectoryAnchor& sessionRoot,
    DirectoryAnchor& staging,
    const QString& finalPath,
    QString& message)
{
    const QString stagingName =
        QString::fromLatin1(
            OverlayStagingDirectoryName);
    const QString finalName =
        QString::fromLatin1(
            OverlayDirectoryName);
    if (!anchoredPathStillHasIdentity(
            staging.canonicalPath,
            staging.metadata) ||
        !anchoredChildHasIdentity(
            sessionRoot.handle.get(),
            stagingName,
            staging.metadata) ||
        !flushAnchoredOverlayDirectory(
            staging.handle.get(),
            message))
    {
        if (message.isEmpty())
        {
            message = QStringLiteral(
                "Sparse-overlay staging directory changed before publication");
        }
        return false;
    }
    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                BeforeOverlayPublish,
            finalPath))
    {
        message = QStringLiteral(
            "Injected sparse-overlay publish failure");
        return false;
    }
    if (!anchoredPathStillHasIdentity(
            staging.canonicalPath,
            staging.metadata) ||
        !anchoredChildHasIdentity(
            sessionRoot.handle.get(),
            stagingName,
            staging.metadata))
    {
        message = QStringLiteral(
            "Sparse-overlay staging route changed immediately before publication");
        return false;
    }
#ifdef Q_OS_WIN
    const std::wstring nativeFinalName =
        finalName.toStdWString();
    const std::size_t nameBytes =
        nativeFinalName.size() *
        sizeof(wchar_t);
    const std::size_t informationBytes =
        sizeof(FILE_RENAME_INFO) +
        nameBytes;
    std::vector<quint64> storage(
        (informationBytes +
         sizeof(quint64) - 1) /
            sizeof(quint64),
        0);
    auto* renameInformation =
        reinterpret_cast<FILE_RENAME_INFO*>(
            storage.data());
    renameInformation->ReplaceIfExists =
        FALSE;
    renameInformation->RootDirectory =
        sessionRoot.handle.get();
    renameInformation->FileNameLength =
        static_cast<DWORD>(nameBytes);
    std::memcpy(
        renameInformation->FileName,
        nativeFinalName.data(),
        nameBytes);
    IO_STATUS_BLOCK ioStatus = {};
    const NtSetInformationFileFunction
        setInformation =
            nativeNtSetInformationFile();
    const NTSTATUS status =
        setInformation != nullptr
        ? setInformation(
              staging.handle.get(),
              &ioStatus,
              renameInformation,
              static_cast<ULONG>(
                  informationBytes),
              static_cast<
                  FILE_INFORMATION_CLASS>(10))
        : static_cast<NTSTATUS>(-1);
    if (status < 0)
    {
        message = QStringLiteral(
            "Anchored sparse-overlay directory publication failed with status 0x%1")
            .arg(
                static_cast<quint32>(status),
                8,
                16,
                QLatin1Char('0'));
        return false;
    }
#else
    const QByteArray stagingNative =
        QFile::encodeName(stagingName);
    const QByteArray finalNative =
        QFile::encodeName(finalName);
    struct stat stagingInformation = {};
    NativeMetadata namedStagingMetadata;
    QString metadataMessage;
    if (::fstatat(
            sessionRoot.handle.get(),
            stagingNative.constData(),
            &stagingInformation,
            AT_SYMLINK_NOFOLLOW) != 0 ||
        !metadataFromPosixStat(
            stagingInformation,
            namedStagingMetadata,
            metadataMessage) ||
        !sameIdentity(
            staging.metadata,
            namedStagingMetadata))
    {
        message = QStringLiteral(
            "Sparse-overlay staging route changed before publication");
        return false;
    }
    struct stat finalInformation = {};
    if (::fstatat(
            sessionRoot.handle.get(),
            finalNative.constData(),
            &finalInformation,
            AT_SYMLINK_NOFOLLOW) == 0 ||
        errno != ENOENT)
    {
        message = QStringLiteral(
            "Sparse-overlay publication target already exists or cannot be inspected");
        return false;
    }
    if (::renameat(
            sessionRoot.handle.get(),
            stagingNative.constData(),
            sessionRoot.handle.get(),
            finalNative.constData()) != 0)
    {
        message = QStringLiteral(
            "Cannot atomically publish the sparse-overlay directory (system error %1)")
            .arg(errno);
        return false;
    }
    if (::fsync(
            sessionRoot.handle.get()) != 0)
    {
        message = QStringLiteral(
            "Cannot flush the private session directory after sparse-overlay publication (system error %1)")
            .arg(errno);
        return false;
    }
#endif
    staging.canonicalPath =
        QDir::cleanPath(finalPath);
    if (!exactUtf8(
            staging.canonicalPath,
            staging.canonicalPathUtf8) ||
        !anchoredPathStillHasIdentity(
            staging.canonicalPath,
            staging.metadata) ||
        !anchoredChildHasIdentity(
            sessionRoot.handle.get(),
            finalName,
            staging.metadata) ||
        anchoredChildExists(
            sessionRoot.handle.get(),
            stagingName))
    {
        message = QStringLiteral(
            "Published sparse-overlay directory does not retain the staged anchored identity");
        return false;
    }
    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                AfterOverlayPublish,
            finalPath))
    {
        message = QStringLiteral(
            "Injected failure after complete sparse-overlay publication");
        return false;
    }
    return true;
}

bool verifyOverlayFile(
    const ListedChild& actual,
    const PreparedDesktopRunOverlayFile& expected,
    QString& message)
{
    if (actual.metadata.kind !=
            NativeNodeKind::RegularFile ||
        actual.metadata.linkCount != 1 ||
        actual.metadata.size !=
            static_cast<quint64>(
                expected.bytes.size()))
    {
        message = QStringLiteral(
            "Published sparse-overlay file is linked, hard-linked, or has an unexpected size");
        return false;
    }
#ifdef Q_OS_WIN
    LARGE_INTEGER start = {};
    if (!SetFilePointerEx(
            actual.handle.get(),
            start,
            nullptr,
            FILE_BEGIN))
    {
        message = QStringLiteral(
            "Cannot rewind a published sparse-overlay file");
        return false;
    }
#endif
    QCryptographicHash hash(
        QCryptographicHash::Sha256);
    QByteArray buffer(
        static_cast<qsizetype>(
            (std::min)(
                static_cast<quint64>(
                    HashBufferBytes),
                actual.metadata.size)),
        Qt::Uninitialized);
    quint64 offset = 0;
    while (offset < actual.metadata.size)
    {
        const qsizetype requested =
            static_cast<qsizetype>(
                (std::min)(
                    actual.metadata.size -
                        offset,
                    static_cast<quint64>(
                        buffer.size())));
#ifdef Q_OS_WIN
        DWORD read = 0;
        if (!ReadFile(
                actual.handle.get(),
                buffer.data(),
                static_cast<DWORD>(
                    requested),
                &read,
                nullptr) ||
            read !=
                static_cast<DWORD>(
                    requested))
        {
            message = QStringLiteral(
                "Cannot read a published sparse-overlay file");
            return false;
        }
        const qsizetype current =
            static_cast<qsizetype>(read);
#else
        ssize_t read = -1;
        do
        {
            read = ::pread(
                actual.handle.get(),
                buffer.data(),
                static_cast<std::size_t>(
                    requested),
                static_cast<off_t>(
                    offset));
        }
        while (read < 0 &&
               errno == EINTR);
        if (read != requested)
        {
            message = QStringLiteral(
                "Cannot read a published sparse-overlay file");
            return false;
        }
        const qsizetype current =
            static_cast<qsizetype>(read);
#endif
        if (std::memcmp(
                buffer.constData(),
                expected.bytes.constData() +
                    static_cast<qsizetype>(
                        offset),
                static_cast<std::size_t>(
                    current)) != 0)
        {
            message = QStringLiteral(
                "Published sparse-overlay bytes do not match the prepared launch");
            return false;
        }
        hash.addData(
            QByteArrayView(
                buffer.constData(),
                current));
        offset +=
            static_cast<quint64>(
                current);
    }
    NativeMetadata finalMetadata;
    QString metadataMessage;
    if (!metadataFromNativeHandle(
            actual.handle.get(),
            finalMetadata,
            metadataMessage) ||
        !sameStableMetadata(
            actual.metadata,
            finalMetadata) ||
        hash.result() !=
            expected.sha256)
    {
        message = QStringLiteral(
            "Published sparse-overlay content, SHA-256, or file identity changed during verification");
        return false;
    }
    return true;
}

bool verifyPublishedOverlayTree(
    DirectoryAnchor& directory,
    const ValidatedOverlayNode& expected,
    const DesktopRunSessionWorkspaceLimits& limits,
    QString& problemPath,
    QString& message)
{
    ScanContext enumeration;
    enumeration.limits = limits;
    QVector<QString> names;
    if (!enumerateDirectoryNames(
            directory.handle.get(),
            names,
            enumeration,
            directory.canonicalPath,
            static_cast<quint64>(
                expected.children.size())))
    {
        problemPath =
            enumeration.problemPath.isEmpty()
            ? directory.canonicalPath
            : enumeration.problemPath;
        message =
            enumeration.message.isEmpty()
            ? QStringLiteral(
                  "Cannot enumerate the published sparse-overlay directory")
            : enumeration.message;
        return false;
    }
    std::sort(
        names.begin(),
        names.end(),
        [](const QString& left,
           const QString& right)
        {
            return left.toUtf8() <
                right.toUtf8();
        });
    if (names.size() !=
        static_cast<qsizetype>(
            expected.children.size()))
    {
        problemPath = directory.canonicalPath;
        message = QStringLiteral(
            "Published sparse-overlay directory contains an unexpected entry set");
        return false;
    }
    for (qsizetype index = 0;
         index < names.size();
         ++index)
    {
        const ValidatedOverlayNode& expectedChild =
            expected.children.at(
                static_cast<std::size_t>(
                    index));
        if (names.at(index) !=
            expectedChild.name)
        {
            problemPath = directory.canonicalPath;
            message = QStringLiteral(
                "Published sparse-overlay entry spelling or topology does not match the prepared launch");
            return false;
        }

        ListedChild actual;
        actual.name = names.at(index);
        actual.nameUtf8 =
            actual.name.toUtf8();
        actual.displayPath =
            QDir(directory.canonicalPath).
                filePath(actual.name);
        ScanContext openContext;
        openContext.limits = limits;
        if (!openAnchoredChild(
                directory.handle.get(),
                actual,
                openContext))
        {
            problemPath = actual.displayPath;
            message =
                openContext.message.isEmpty()
                ? QStringLiteral(
                      "Cannot anchor a published sparse-overlay entry")
                : openContext.message;
            return false;
        }
        if (expectedChild.file != nullptr)
        {
            if (!verifyOverlayFile(
                    actual,
                    *expectedChild.file,
                    message))
            {
                problemPath = actual.displayPath;
                return false;
            }
        }
        else
        {
            if (actual.metadata.kind !=
                    NativeNodeKind::Directory ||
                actual.metadata.device !=
                    directory.metadata.device)
            {
                problemPath = actual.displayPath;
                message = QStringLiteral(
                    "Published sparse-overlay directory is linked, reparsed, or routed to another device");
                return false;
            }
#ifdef Q_OS_WIN
            NativeHandle permissionHandle;
            NativeMetadata permissionMetadata;
            QString permissionOpenMessage;
            if (!ntOpenRelative(
                    directory.handle.get(),
                    actual.name.toStdWString(),
                    FILE_LIST_DIRECTORY |
                        FILE_READ_ATTRIBUTES |
                        READ_CONTROL |
                        SYNCHRONIZE,
                    FILE_OPEN,
                    FILE_DIRECTORY_FILE,
                    FILE_ATTRIBUTE_NORMAL,
                    permissionHandle,
                    nullptr,
                    permissionOpenMessage) ||
                !metadataFromNativeHandle(
                    permissionHandle.get(),
                    permissionMetadata,
                    permissionOpenMessage) ||
                !sameIdentity(
                    actual.metadata,
                    permissionMetadata))
            {
                problemPath = actual.displayPath;
                message = permissionOpenMessage.isEmpty()
                    ? QStringLiteral(
                          "Cannot reopen a published sparse-overlay directory with its exact identity")
                    : permissionOpenMessage;
                return false;
            }
            actual.handle =
                std::move(permissionHandle);
            QString permissionMessage;
            if (!trustedWindowsDirectoryPermissions(
                    actual.handle.get(),
                    permissionMessage))
#else
            QString permissionMessage;
            if (!trustedPosixDirectoryPermissions(
                    actual.handle.get(),
                    permissionMessage))
#endif
            {
                problemPath = actual.displayPath;
                message = permissionMessage;
                return false;
            }
            DirectoryAnchor childDirectory;
            childDirectory.canonicalPath =
                actual.displayPath;
            childDirectory.canonicalPathUtf8 =
                actual.displayPath.toUtf8();
            childDirectory.metadata =
                actual.metadata;
            childDirectory.handle =
                std::move(actual.handle);
            if (!verifyPublishedOverlayTree(
                    childDirectory,
                    expectedChild,
                    limits,
                    problemPath,
                    message))
            {
                return false;
            }
        }
        if (!anchoredChildStillMatches(
                directory.handle.get(),
                actual))
        {
            problemPath = actual.displayPath;
            message = QStringLiteral(
                "Published sparse-overlay entry changed during anchored verification");
            return false;
        }
    }
    if (!anchoredPathStillHasIdentity(
            directory.canonicalPath,
            directory.metadata))
    {
        problemPath = directory.canonicalPath;
        message = QStringLiteral(
            "Published sparse-overlay directory route changed during verification");
        return false;
    }
    return true;
}
}

static DesktopRunSessionWorkspaceResult
createDesktopRunSessionWorkspaceImpl(
    const QString& trustedSessionsBaseDirectory,
    const EditorRun::Descriptor& descriptorTemplate,
    const DesktopRunSessionFormalRoots& formalRoots,
    const ValidatedPreparedOverlay* preparedOverlay,
    const DesktopRunSessionWorkspaceLimits& limits,
    const DesktopRunSessionWorkspaceControl& control)
{
    DesktopRunSessionWorkspaceResult result;
    const auto failIfCancelled =
        [&](const QString& path)
        {
            if (!control.cancellationRequested ||
                !control.cancellationRequested())
            {
                return false;
            }
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    Cancelled,
                path,
                QStringLiteral(
                    "Desktop-run workspace creation was cancelled"));
            return true;
        };
    if (failIfCancelled({}))
        return result;
    if (limits.maximumFormalRootCount <= 0 ||
        limits.maximumEntryCount == 0 ||
        limits.maximumManifestBytes <= 0 ||
        limits.maximumPathBytes <= 0 ||
        limits.maximumDirectoryDepth <= 0 ||
        limits.maximumDirectoryEntryCount == 0 ||
        limits.maximumWorkingBytes == 0)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::InvalidLimits,
            {},
            QStringLiteral(
                "Desktop-run workspace limits must all be positive"));
        return result;
    }

    DirectoryAnchor sessionsBase;
    if (!validatePrivateRoot(
            trustedSessionsBaseDirectory,
            limits,
            sessionsBase,
            result))
    {
        return result;
    }
    result.trustedSessionsBaseDirectory =
        sessionsBase.canonicalPath;
    if (formalRoots.resourceRoots.isEmpty() ||
        formalRoots.saveRoot.isEmpty())
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::NoRoutingRoots,
            {},
            QStringLiteral(
                "Formal resource roots and the current active-root save role are required"));
        return result;
    }
    const qsizetype requestedFormalRootCount =
        formalRoots.resourceRoots.size() + 1;
    if (requestedFormalRootCount >
        limits.maximumFormalRootCount)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::TooManyRoutingRoots,
            {},
            QStringLiteral(
                "Formal root count exceeds the configured limit"));
        return result;
    }

    std::vector<FormalRoot> routingRoots;
    routingRoots.reserve(
        static_cast<std::size_t>(
            requestedFormalRootCount));
    const auto sameRoutingPath =
        [](const QString& left,
           const QString& right)
        {
            return left.compare(
                       right,
                       EditorAssetPath::caseSensitivity(
                           left)) == 0;
        };
    const auto addFormalRoot =
        [&](const QString& formalRootPath,
            bool resourceRole,
            bool saveRole) -> bool
    {
        if (formalRootPath.isEmpty() ||
            !QDir::isAbsolutePath(formalRootPath))
        {
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    InvalidRoutingRootPath,
                formalRootPath,
                QStringLiteral(
                    "Formal root routing path must be absolute"));
            return false;
        }
        const QString path =
            normalizedAbsolutePath(formalRootPath);
        QByteArray pathUtf8;
        if (path.isEmpty() ||
            !exactUtf8(path, pathUtf8) ||
            pathUtf8.size() > limits.maximumPathBytes)
        {
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    InvalidRoutingRootPath,
                path,
                QStringLiteral(
                    "Formal root routing path is not valid bounded UTF-8"));
            return false;
        }
        for (FormalRoot& existing :
             routingRoots)
        {
            if (sameRoutingPath(
                    existing.path,
                    path))
            {
                existing.resourceRole =
                    existing.resourceRole ||
                    resourceRole;
                existing.saveRole =
                    existing.saveRole ||
                    saveRole;
                return true;
            }
        }
        FormalRoot formalRoot;
        formalRoot.path = path;
        formalRoot.pathUtf8 = pathUtf8;
        formalRoot.resourceRole =
            resourceRole;
        formalRoot.saveRole = saveRole;
        routingRoots.push_back(
            std::move(formalRoot));
        return true;
    };
    for (qsizetype index = 0;
         index < formalRoots.resourceRoots.size();
         ++index)
    {
        if (!addFormalRoot(
                formalRoots.resourceRoots.at(index),
                true,
                false))
        {
            return result;
        }
    }
    if (!addFormalRoot(
            formalRoots.saveRoot,
            false,
            true))
    {
        return result;
    }
    std::sort(
        routingRoots.begin(),
        routingRoots.end(),
        [](const FormalRoot& left,
           const FormalRoot& right)
        {
            return left.pathUtf8 <
                right.pathUtf8;
        });

    QString descriptorAssetsPath;
    QByteArray descriptorAssetsUtf8;
    if (!pathText(
            descriptorTemplate.assetsCollectionRoot,
            descriptorAssetsPath,
            descriptorAssetsUtf8))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                RoutingRootsDoNotCoverDescriptorAssets,
            {},
            QStringLiteral(
                "Descriptor assetsCollectionRoot is not valid UTF-8"));
        return result;
    }
    if (descriptorAssetsPath.isEmpty() ||
        !QDir::isAbsolutePath(descriptorAssetsPath))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                RoutingRootsDoNotCoverDescriptorAssets,
            descriptorAssetsPath,
            QStringLiteral(
                "Descriptor assetsCollectionRoot must be an absolute routing path"));
        return result;
    }
    descriptorAssetsPath =
        normalizedAbsolutePath(descriptorAssetsPath);
    if (descriptorAssetsPath.isEmpty() ||
        descriptorAssetsUtf8.size() >
            limits.maximumPathBytes)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                RoutingRootsDoNotCoverDescriptorAssets,
            descriptorAssetsPath,
            QStringLiteral(
                "Descriptor assetsCollectionRoot is not a bounded routing path"));
        return result;
    }
    bool descriptorAssetsCovered = false;
    for (const FormalRoot& formalRoot :
         routingRoots)
    {
        if (formalRoot.resourceRole &&
            sameRoutingPath(
                formalRoot.path,
                descriptorAssetsPath))
        {
            descriptorAssetsCovered = true;
            break;
        }
    }
    if (!descriptorAssetsCovered)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                RoutingRootsDoNotCoverDescriptorAssets,
            descriptorAssetsPath,
            QStringLiteral(
                "Descriptor assetsCollectionRoot is not listed as a resource routing path"));
        return result;
    }

    const QVector<DesktopRunTraceOverlayOrigin>
        emptyTraceOverlayOrigins;
    const QVector<DesktopRunTraceOverlayOrigin>&
        traceOverlayOrigins =
            preparedOverlay != nullptr
        ? preparedOverlay->origins
        : emptyTraceOverlayOrigins;
    QByteArray resourceRoutingContractBytes;
    QString resourceRoutingContractProblemPath;
    QString resourceRoutingContractMessage;
    if (!serializeResourceRoutingContract(
            routingRoots,
            traceOverlayOrigins,
            limits,
            resourceRoutingContractBytes,
            resourceRoutingContractProblemPath,
            resourceRoutingContractMessage))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                ResourceRoutingContractWriteFailed,
            resourceRoutingContractProblemPath,
            resourceRoutingContractMessage);
        return result;
    }
    if (failIfCancelled(
            sessionsBase.canonicalPath))
    {
        return result;
    }

    EditorRun::Descriptor descriptor;
    QByteArray descriptorBytes;
    DirectoryAnchor sessionRootAnchor;
    QString sessionCreationMessage;
    bool sessionCreated = false;
    bool sessionRootMaterialized = false;
    for (int attempt = 0;
         attempt < MaximumSessionCreationAttempts;
         ++attempt)
    {
        result.sessionId =
            QUuid::createUuid()
                .toString(QUuid::WithoutBraces)
                .toLower();
        const QString sessionRoot =
            QDir(sessionsBase.canonicalPath).filePath(
                result.sessionId);
        result.paths =
            pathsForSession(sessionRoot);
        descriptor = materializeDescriptor(
            descriptorTemplate,
            result.sessionId,
            result.paths);
        const EditorRun::DescriptorSerializationResult
            serialization =
                EditorRun::serializeEditorRunDescriptor(
                    descriptor);
        if (!serialization.succeeded())
        {
            result.descriptorError =
                serialization.error;
            result.descriptorFieldPath =
                QString::fromUtf8(
                    serialization.fieldPath);
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    InvalidDescriptor,
                result.paths.descriptorPath,
                QString::fromUtf8(
                    serialization.message));
            result.sessionId.clear();
            result.paths = {};
            return result;
        }
        descriptorBytes = QByteArray(
            serialization.bytes.data(),
            static_cast<qsizetype>(
                serialization.bytes.size()));

        sessionRootAnchor =
            DirectoryAnchor();
        sessionCreationMessage.clear();
        bool candidateMaterialized = false;
        if (createAnchoredDirectory(
                sessionsBase.handle.get(),
                result.sessionId,
                result.paths.sessionRoot,
                sessionRootAnchor,
                candidateMaterialized,
                sessionCreationMessage))
        {
            sessionCreated = true;
            result.partialFailureRoot =
                result.paths.sessionRoot;
            break;
        }
        if (candidateMaterialized)
        {
            sessionRootMaterialized = true;
            if (sessionRootAnchor.handle.valid() &&
                sessionRootAnchor.metadata.kind ==
                    NativeNodeKind::Directory &&
                anchoredPathStillHasIdentity(
                    result.paths.sessionRoot,
                    sessionRootAnchor.metadata))
            {
                result.partialFailureRoot =
                    result.paths.sessionRoot;
            }
            break;
        }
        if (!anchoredChildExists(
                sessionsBase.handle.get(),
                result.sessionId))
        {
            break;
        }
    }
    if (!sessionCreated)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                SessionRootCreationFailed,
            result.paths.sessionRoot,
            sessionCreationMessage.isEmpty()
            ? QStringLiteral(
                  "Cannot atomically create a unique editor-run session root")
            : sessionCreationMessage);
        if (!sessionRootMaterialized)
        {
            result.sessionId.clear();
            result.paths = {};
        }
        return result;
    }

    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                AfterSessionRootOpened,
            result.paths.sessionRoot))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                SessionRoutingChanged,
            result.paths.sessionRoot,
            QStringLiteral(
                "Injected session-root routing change"));
        result.partialFailureRoot.clear();
        result.cleanupWorkspace.reset();
        return result;
    }
    if (!anchoredPathStillHasIdentity(
            sessionsBase.canonicalPath,
            sessionsBase.metadata) ||
        !anchoredPathStillHasIdentity(
            result.paths.sessionRoot,
            sessionRootAnchor.metadata))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                SessionRoutingChanged,
            result.paths.sessionRoot,
            QStringLiteral(
                "Sessions base or session root changed after anchored creation"));
        result.partialFailureRoot.clear();
        result.cleanupWorkspace.reset();
        return result;
    }

    const QByteArray markerBytes =
        QByteArray("{\"schemaVersion\":") +
        QByteArray::number(
            DesktopRunSessionWorkspace::SchemaVersion) +
        ",\"descriptorSchemaVersion\":" +
        QByteArray::number(
            EditorRun::Descriptor::SchemaVersion) +
        ",\"sessionId\":\"" +
        result.sessionId.toLatin1() +
        "\"}";
    QString writeMessage;
    if (!writeAtomically(
            sessionRootAnchor.handle.get(),
            QString::fromLatin1(
                MarkerFileName),
            result.paths.markerPath,
            markerBytes,
            writeMessage))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                MarkerWriteFailed,
            result.paths.markerPath,
            writeMessage);
        return result;
    }
    if (!anchoredPathStillHasIdentity(
            sessionsBase.canonicalPath,
            sessionsBase.metadata) ||
        !anchoredPathStillHasIdentity(
            result.paths.sessionRoot,
            sessionRootAnchor.metadata))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                SessionRoutingChanged,
            result.paths.sessionRoot,
            QStringLiteral(
                "Sessions base or session root changed while writing the marker"));
        result.partialFailureRoot.clear();
        return result;
    }

    DesktopRunSessionWorkspace cleanupWorkspace;
    cleanupWorkspace.sessionId = result.sessionId;
    cleanupWorkspace.paths = result.paths;
    cleanupWorkspace.descriptor = descriptor;
    cleanupWorkspace.descriptorSha256 =
        QCryptographicHash::hash(
            descriptorBytes,
            QCryptographicHash::Sha256)
            .toHex();
    cleanupWorkspace.resourceRoutingContractSha256 =
        QCryptographicHash::hash(
            resourceRoutingContractBytes,
            QCryptographicHash::Sha256)
            .toHex();
    cleanupWorkspace.formalRoots = formalRoots;
    cleanupWorkspace.workspaceLimits = limits;
    result.cleanupWorkspace =
        std::move(cleanupWorkspace);

    if (failIfCancelled(
            result.paths.sessionRoot))
    {
        return result;
    }

    const bool stagePreparedOverlay =
        preparedOverlay != nullptr &&
        !preparedOverlay->root.children.empty();
    const QString overlayStagingPath =
        QDir(result.paths.sessionRoot).filePath(
            QString::fromLatin1(
                OverlayStagingDirectoryName));
    const std::array<QString, 4> leafNames = {
        QString::fromLatin1(
            stagePreparedOverlay
            ? OverlayStagingDirectoryName
            : OverlayDirectoryName),
        QString::fromLatin1(
            SaveDirectoryName),
        QString::fromLatin1(
            ApplicationStateDirectoryName),
        QString::fromLatin1(
            DiagnosticsDirectoryName)
    };
    const std::array<QString, 4> leafPaths = {
        stagePreparedOverlay
        ? overlayStagingPath
        : result.paths.overlayRoot,
        result.paths.isolatedSaveRoot,
        result.paths.applicationStateRoot,
        result.paths.diagnosticsRoot
    };
    std::array<DirectoryAnchor, 4>
        leafAnchors;
    for (std::size_t index = 0;
         index < leafPaths.size();
         ++index)
    {
        if (!createLeafDirectory(
                sessionRootAnchor,
                leafNames[index],
                leafPaths[index],
                leafAnchors[index],
                result))
        {
            return result;
        }
    }
    if (stagePreparedOverlay)
    {
        if (injectFailure(
                DesktopRunSessionWorkspaceFaultPoint::
                    BeforeOverlayStaging,
                overlayStagingPath))
        {
            if (failIfRoutingChanged(
                    result,
                    sessionsBase,
                    sessionRootAnchor,
                    leafAnchors,
                    QStringLiteral(
                        "Routing changed before sparse-overlay staging")))
            {
                return result;
            }
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    OverlayMaterializationFailed,
                overlayStagingPath,
                QStringLiteral(
                    "Injected failure before sparse-overlay staging"));
            return result;
        }
        QString overlayProblemPath;
        QString overlayMessage;
        if (!materializeOverlayTree(
                leafAnchors[0],
                sessionRootAnchor.metadata.device,
                preparedOverlay->root,
                overlayProblemPath,
                overlayMessage))
        {
            if (failIfRoutingChanged(
                    result,
                    sessionsBase,
                    sessionRootAnchor,
                    leafAnchors,
                    QStringLiteral(
                        "Routing changed while staging the sparse overlay")))
            {
                return result;
            }
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    OverlayMaterializationFailed,
                overlayProblemPath.isEmpty()
                    ? overlayStagingPath
                    : overlayProblemPath,
                overlayMessage.isEmpty()
                    ? QStringLiteral(
                          "Cannot materialize the prepared sparse overlay")
                    : overlayMessage);
            return result;
        }
        if (failIfCancelled(
                overlayStagingPath))
        {
            return result;
        }
        if (!publishOverlayDirectory(
                sessionRootAnchor,
                leafAnchors[0],
                result.paths.overlayRoot,
                overlayMessage))
        {
            if (failIfRoutingChanged(
                    result,
                    sessionsBase,
                    sessionRootAnchor,
                    leafAnchors,
                    QStringLiteral(
                        "Routing changed while publishing the sparse overlay")))
            {
                return result;
            }
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    OverlayPublishFailed,
                result.paths.overlayRoot,
                overlayMessage.isEmpty()
                    ? QStringLiteral(
                          "Cannot atomically publish the prepared sparse overlay")
                    : overlayMessage);
            return result;
        }
        if (!verifyPublishedOverlayTree(
                leafAnchors[0],
                preparedOverlay->root,
                limits,
                overlayProblemPath,
                overlayMessage))
        {
            if (failIfRoutingChanged(
                    result,
                    sessionsBase,
                    sessionRootAnchor,
                    leafAnchors,
                    QStringLiteral(
                        "Routing changed while verifying the published sparse overlay")))
            {
                return result;
            }
            fail(
                result,
                DesktopRunSessionWorkspaceError::
                    OverlayPublishFailed,
                overlayProblemPath.isEmpty()
                    ? result.paths.overlayRoot
                    : overlayProblemPath,
                overlayMessage.isEmpty()
                    ? QStringLiteral(
                          "Published sparse overlay failed exact anchored verification")
                    : overlayMessage);
            return result;
        }
    }
    if (injectFailure(
            DesktopRunSessionWorkspaceFaultPoint::
                AfterSessionLeavesOpened,
            result.paths.sessionRoot))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                SessionRoutingChanged,
            result.paths.sessionRoot,
            QStringLiteral(
                "Injected session-leaf routing change"));
        result.partialFailureRoot.clear();
        result.cleanupWorkspace.reset();
        return result;
    }
    if (failIfRoutingChanged(
            result,
            sessionsBase,
            sessionRootAnchor,
            leafAnchors,
            QStringLiteral(
                "Anchored session validation failed")))
    {
        return result;
    }

    if (failIfCancelled(
            result.paths.resourceRoutingContractPath))
    {
        return result;
    }
    if (!writeAtomically(
            sessionRootAnchor.handle.get(),
            QString::fromLatin1(
                ResourceRoutingContractFileName),
            result.paths.resourceRoutingContractPath,
            resourceRoutingContractBytes,
            writeMessage))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                ResourceRoutingContractWriteFailed,
            result.paths.resourceRoutingContractPath,
            writeMessage);
        return result;
    }
    if (failIfRoutingChanged(
            result,
            sessionsBase,
            sessionRootAnchor,
            leafAnchors,
            QStringLiteral(
                "Routing changed while writing the resource-routing contract")))
    {
        return result;
    }

    if (failIfCancelled(
            result.paths.descriptorPath))
    {
        return result;
    }
    if (!writeAtomically(
            sessionRootAnchor.handle.get(),
            QString::fromLatin1(
                DescriptorFileName),
            result.paths.descriptorPath,
            descriptorBytes,
            writeMessage))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                DescriptorWriteFailed,
            result.paths.descriptorPath,
            writeMessage);
        return result;
    }
    if (failIfRoutingChanged(
            result,
            sessionsBase,
            sessionRootAnchor,
            leafAnchors,
            QStringLiteral(
                "Routing changed while writing the launch descriptor")))
    {
        return result;
    }

    DesktopRunSessionWorkspace workspace;
    workspace.sessionId = result.sessionId;
    workspace.paths = result.paths;
    workspace.descriptor = std::move(descriptor);
    workspace.descriptorSha256 =
        QCryptographicHash::hash(
            descriptorBytes,
            QCryptographicHash::Sha256)
            .toHex();
    workspace.resourceRoutingContractSha256 =
        QCryptographicHash::hash(
            resourceRoutingContractBytes,
            QCryptographicHash::Sha256)
            .toHex();
    workspace.traceOverlayOrigins =
        traceOverlayOrigins;
    workspace.formalRoots = formalRoots;
    workspace.workspaceLimits = limits;
    result.workspace = std::move(workspace);
    result.partialFailureRoot.clear();
    return result;
}

DesktopRunSessionWorkspaceResult createDesktopRunSessionWorkspace(
    const QString& trustedSessionsBaseDirectory,
    const EditorRun::Descriptor& descriptorTemplate,
    const DesktopRunSessionFormalRoots& formalRoots,
    const DesktopRunSessionWorkspaceLimits& limits,
    const DesktopRunSessionWorkspaceControl& control)
{
    return createDesktopRunSessionWorkspaceImpl(
        trustedSessionsBaseDirectory,
        descriptorTemplate,
        formalRoots,
        nullptr,
        limits,
        control);
}

DesktopRunSessionWorkspaceResult createDesktopRunSessionWorkspace(
    const QString& trustedSessionsBaseDirectory,
    const PreparedSavedSceneLaunch& preparedLaunch,
    const DesktopRunSessionWorkspaceLimits& limits,
    const DesktopRunSessionWorkspaceControl& control)
{
    EditorRun::Descriptor descriptorTemplate;
    if (!descriptorTemplateForPreparedLaunch(
            preparedLaunch,
            descriptorTemplate))
    {
        DesktopRunSessionWorkspaceResult result;
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                PreparedLaunchDescriptorMismatch,
            {},
            QStringLiteral(
                "Prepared saved-scene launch cannot be represented by the versioned descriptor schema"));
        return result;
    }
    return createDesktopRunSessionWorkspace(
        trustedSessionsBaseDirectory,
        descriptorTemplate,
        preparedLaunch,
        limits,
        control);
}

DesktopRunSessionWorkspaceResult createDesktopRunSessionWorkspace(
    const QString& trustedSessionsBaseDirectory,
    const EditorRun::Descriptor& descriptorTemplate,
    const PreparedSavedSceneLaunch& preparedLaunch,
    const DesktopRunSessionWorkspaceLimits& limits,
    const DesktopRunSessionWorkspaceControl& control)
{
    DesktopRunSessionWorkspaceResult result;
    QString descriptorAssetsPath;
    QByteArray descriptorAssetsUtf8;
    const QString preparedAssetsPath =
        EditorAssetPath::normalizedAbsolutePath(
            preparedLaunch.assetsCollectionRoot);
    if (!pathText(
            descriptorTemplate.assetsCollectionRoot,
            descriptorAssetsPath,
            descriptorAssetsUtf8))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                PreparedLaunchDescriptorMismatch,
            descriptorAssetsPath,
            QStringLiteral(
                "Launch descriptor assetsCollectionRoot is not valid UTF-8"));
        return result;
    }
    const QString normalizedDescriptorAssetsPath =
        EditorAssetPath::normalizedAbsolutePath(
            descriptorAssetsPath);
    if (preparedAssetsPath.isEmpty() ||
        normalizedDescriptorAssetsPath.compare(
            preparedAssetsPath,
            EditorAssetPath::caseSensitivity(
                preparedAssetsPath)) != 0)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                PreparedLaunchDescriptorMismatch,
            descriptorAssetsPath,
            QStringLiteral(
                "Launch descriptor assetsCollectionRoot does not match the prepared resource selection"));
        return result;
    }

    EditorRun::Descriptor expectedDescriptor;
    if (!descriptorTemplateForPreparedLaunch(
            preparedLaunch,
            expectedDescriptor) ||
        !descriptorBusinessFieldsMatch(
            descriptorTemplate,
            expectedDescriptor))
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                PreparedLaunchDescriptorMismatch,
            {},
            QStringLiteral(
                "Launch descriptor business fields do not match the prepared launch target"));
        return result;
    }

    DesktopRunSessionFormalRoots formalRoots;
    formalRoots.resourceRoots =
        preparedLaunch.formalRoots;
    formalRoots.saveRoot =
        preparedLaunch.activeContentRoot;
    if (limits.maximumFormalRootCount <= 0 ||
        limits.maximumEntryCount == 0 ||
        limits.maximumManifestBytes <= 0 ||
        limits.maximumPathBytes <= 0 ||
        limits.maximumDirectoryDepth <= 0 ||
        limits.maximumDirectoryEntryCount == 0 ||
        limits.maximumWorkingBytes == 0)
    {
        fail(
            result,
            DesktopRunSessionWorkspaceError::
                InvalidLimits,
            {},
            QStringLiteral(
                "Desktop-run workspace limits must all be positive"));
        return result;
    }
    ValidatedPreparedOverlay
        validatedOverlay;
    if (!validatePreparedOverlay(
            preparedLaunch,
            limits,
            validatedOverlay,
            result))
    {
        return result;
    }
    result = createDesktopRunSessionWorkspaceImpl(
        trustedSessionsBaseDirectory,
        descriptorTemplate,
        formalRoots,
        &validatedOverlay,
        limits,
        control);
    return result;
}

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
void setDesktopRunSessionWorkspaceFaultInjectorForTests(
    DesktopRunSessionWorkspaceFaultInjector injector)
{
    const std::lock_guard<std::mutex> lock(
        faultInjectorMutex());
    faultInjector() = std::move(injector);
}
#endif
