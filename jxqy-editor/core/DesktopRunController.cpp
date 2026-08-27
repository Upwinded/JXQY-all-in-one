#include "DesktopRunController.h"

#include "DesktopRunSessionWorkspace.h"
#include "EditorAssetPath.h"
#include "EditorSettings.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
namespace fs = std::filesystem;

fs::path hostPath(const QString& path)
{
    const QByteArray utf8 = path.toUtf8();
    return fs::u8path(
        utf8.constData(),
        utf8.constData() + utf8.size());
}

QString hostPathText(const fs::path& path)
{
    if (path.empty())
        return {};
    try
    {
#ifdef Q_OS_WIN
        return EditorAssetPath::normalizedAbsolutePath(
            QDir::fromNativeSeparators(
                QString::fromStdWString(path.native())));
#else
        const auto encoded = path.generic_u8string();
        const QByteArray utf8(
            reinterpret_cast<const char*>(
                encoded.data()),
            static_cast<qsizetype>(encoded.size()));
        const QString text =
            QString::fromUtf8(
                utf8.constData(),
                utf8.size());
        if (text.toUtf8() != utf8)
            return {};
        return EditorAssetPath::normalizedAbsolutePath(
            text);
#endif
    }
    catch (...)
    {
        return {};
    }
}

bool isDirectoryLink(const QFileInfo& information)
{
    if (information.isSymLink())
        return true;
#ifdef Q_OS_WIN
    return information.isJunction();
#else
    return false;
#endif
}

bool sameLiteralPath(
    const QString& left,
    const QString& right)
{
    return left.compare(
               right,
               EditorAssetPath::caseSensitivity(left)) == 0;
}

QString canonicalUnlinkedPath(
    const QString& path,
    bool directory)
{
    const QString normalized =
        EditorAssetPath::normalizedAbsolutePath(path);
    const QFileInfo information(normalized);
    if ((directory && !information.isDir()) ||
        (!directory && !information.isFile()) ||
        information.isSymLink() ||
        (directory && isDirectoryLink(information)))
    {
        return {};
    }

    const QString canonicalPath =
        information.canonicalFilePath();
    if (canonicalPath.isEmpty())
        return {};
    const QString canonical =
        EditorAssetPath::normalizedAbsolutePath(
            canonicalPath);
    if (canonical.isEmpty() ||
        !sameLiteralPath(normalized, canonical))
    {
        return {};
    }
    return canonical;
}

bool isLowercaseHexCharacter(char character)
{
    return (character >= '0' && character <= '9') ||
        (character >= 'a' && character <= 'f');
}

bool isLowercaseSha256(const QByteArray& digest)
{
    if (digest.size() != 64)
        return false;
    for (char character : digest)
    {
        if (!isLowercaseHexCharacter(character))
            return false;
    }
    return true;
}

bool isCanonicalLowercaseUuid(const QByteArray& value)
{
    if (value.size() != 36)
        return false;
    for (qsizetype index = 0;
         index < value.size();
         ++index)
    {
        if (index == 8 ||
            index == 13 ||
            index == 18 ||
            index == 23)
        {
            if (value.at(index) != '-')
                return false;
            continue;
        }
        if (!isLowercaseHexCharacter(
                value.at(index)))
        {
            return false;
        }
    }
    return true;
}

bool sameNormalizedPath(
    const QString& left,
    const QString& right)
{
    if (left.isEmpty() || right.isEmpty())
        return false;
    const QString normalizedLeft =
        EditorAssetPath::normalizedAbsolutePath(left);
    const QString normalizedRight =
        EditorAssetPath::normalizedAbsolutePath(right);
    return !normalizedLeft.isEmpty() &&
        !normalizedRight.isEmpty() &&
        sameLiteralPath(
            normalizedLeft,
            normalizedRight);
}

bool workspaceContractIsConsistent(
    const DesktopRunSessionWorkspace& workspace,
    const QString& workingDirectory)
{
    const QByteArray descriptorSessionId(
        workspace.descriptor.sessionId.data(),
        static_cast<qsizetype>(
            workspace.descriptor.sessionId.size()));
    if (!isCanonicalLowercaseUuid(
            workspace.sessionId.toLatin1()) ||
        workspace.sessionId.toUtf8() !=
            descriptorSessionId)
    {
        return false;
    }

    const QDir sessionDirectory(
        workspace.paths.sessionRoot);
    const QDir diagnosticsDirectory(
        workspace.paths.diagnosticsRoot);
    if (!sameNormalizedPath(
            workspace.paths.sessionRoot,
            workingDirectory) ||
        QFileInfo(
            workspace.paths.sessionRoot)
                .fileName() !=
            workspace.sessionId ||
        !sameNormalizedPath(
            workspace.paths.overlayRoot,
            sessionDirectory.filePath(
                QStringLiteral("overlay"))) ||
        !sameNormalizedPath(
            workspace.paths.isolatedSaveRoot,
            sessionDirectory.filePath(
                QStringLiteral("save"))) ||
        !sameNormalizedPath(
            workspace.paths.applicationStateRoot,
            sessionDirectory.filePath(
                QStringLiteral(
                    "application-state"))) ||
        !sameNormalizedPath(
            workspace.paths.diagnosticsRoot,
            sessionDirectory.filePath(
                QStringLiteral("diagnostics"))) ||
        !sameNormalizedPath(
            workspace.paths.diagnosticsPath,
            diagnosticsDirectory.filePath(
                QStringLiteral(
                    "diagnostics.jsonl"))) ||
        !sameNormalizedPath(
            workspace.paths.logPath,
            diagnosticsDirectory.filePath(
                QStringLiteral("game.log"))) ||
        !sameNormalizedPath(
            workspace.paths.runtimeTracePath,
            diagnosticsDirectory.filePath(
                QStringLiteral(
                    "runtime-trace.jsonl"))) ||
        !sameNormalizedPath(
            workspace.paths.markerPath,
            sessionDirectory.filePath(
                QStringLiteral(
                    "session-marker.json"))) ||
        !sameNormalizedPath(
            workspace.paths.resourceRoutingContractPath,
            sessionDirectory.filePath(
                QStringLiteral(
                    "resource-routing-contract.json"))) ||
        !sameNormalizedPath(
            workspace.paths.descriptorPath,
            sessionDirectory.filePath(
                QStringLiteral(
                    "launch-descriptor.json"))))
    {
        return false;
    }

    const QString descriptorOverlayRoot =
        hostPathText(
            workspace.descriptor.overlayRoot);
    const QString descriptorSaveRoot =
        hostPathText(
            workspace.descriptor.isolatedSaveRoot);
    const QString descriptorApplicationStateRoot =
        hostPathText(
            workspace.descriptor.applicationStateRoot);
    const QString descriptorDiagnosticsPath =
        hostPathText(
            workspace.descriptor.diagnosticsPath);
    const QString descriptorLogPath =
        hostPathText(
            workspace.descriptor.logPath);
    return !descriptorOverlayRoot.isEmpty() &&
        !descriptorSaveRoot.isEmpty() &&
        !descriptorApplicationStateRoot.isEmpty() &&
        !descriptorDiagnosticsPath.isEmpty() &&
        !descriptorLogPath.isEmpty() &&
        sameNormalizedPath(
            descriptorOverlayRoot,
            workspace.paths.overlayRoot) &&
        sameNormalizedPath(
            descriptorSaveRoot,
            workspace.paths.isolatedSaveRoot) &&
        sameNormalizedPath(
            descriptorApplicationStateRoot,
            workspace.paths.applicationStateRoot) &&
        sameNormalizedPath(
            descriptorDiagnosticsPath,
            workspace.paths.diagnosticsPath) &&
        sameNormalizedPath(
            descriptorLogPath,
            workspace.paths.logPath);
}

bool hasSingleHardLink(const QString& path)
{
    std::error_code error;
    const std::uintmax_t linkCount =
        fs::hard_link_count(hostPath(path), error);
    return !error && linkCount == 1;
}

QString finalizedDecoderText(QStringDecoder& decoder)
{
    QChar buffer[4] = {};
    const QStringDecoder::FinalizeResultQChar result =
        decoder.finalize(buffer, 4);
    return QString(
        buffer,
        static_cast<qsizetype>(result.next - buffer));
}

#ifdef Q_OS_WIN
struct NativeFileIdentity
{
    DWORD volumeSerialNumber = 0;
    DWORD fileIndexHigh = 0;
    DWORD fileIndexLow = 0;
    DWORD linkCount = 0;
    bool directory = false;
};

bool queryNativeFileIdentity(
    HANDLE handle,
    NativeFileIdentity* identity)
{
    BY_HANDLE_FILE_INFORMATION information = {};
    if (handle == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(
            handle,
            &information))
    {
        return false;
    }
    identity->volumeSerialNumber =
        information.dwVolumeSerialNumber;
    identity->fileIndexHigh = information.nFileIndexHigh;
    identity->fileIndexLow = information.nFileIndexLow;
    identity->linkCount = information.nNumberOfLinks;
    identity->directory =
        (information.dwFileAttributes &
         FILE_ATTRIBUTE_DIRECTORY) != 0;
    return (information.dwFileAttributes &
            FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

bool sameNativeFileIdentity(
    const NativeFileIdentity& left,
    const NativeFileIdentity& right)
{
    return left.volumeSerialNumber ==
            right.volumeSerialNumber &&
        left.fileIndexHigh == right.fileIndexHigh &&
        left.fileIndexLow == right.fileIndexLow &&
        left.linkCount == right.linkCount &&
        left.directory == right.directory;
}

bool digestOpenedRegularFile(
    HANDLE handle,
    const NativeFileIdentity& expectedIdentity,
    qint64* contentSize,
    QByteArray* contentDigest)
{
    NativeFileIdentity beforeIdentity;
    LARGE_INTEGER beforeSize = {};
    LARGE_INTEGER beginning = {};
    if (handle == INVALID_HANDLE_VALUE ||
        !queryNativeFileIdentity(
            handle,
            &beforeIdentity) ||
        beforeIdentity.directory ||
        !sameNativeFileIdentity(
            expectedIdentity,
            beforeIdentity) ||
        !GetFileSizeEx(handle, &beforeSize) ||
        beforeSize.QuadPart < 0 ||
        !SetFilePointerEx(
            handle,
            beginning,
            nullptr,
            FILE_BEGIN))
    {
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    char buffer[64 * 1024];
    LONGLONG remaining = beforeSize.QuadPart;
    while (remaining > 0)
    {
        const DWORD requested =
            remaining <
                static_cast<LONGLONG>(sizeof(buffer))
            ? static_cast<DWORD>(remaining)
            : static_cast<DWORD>(sizeof(buffer));
        DWORD readCount = 0;
        if (!ReadFile(
                handle,
                buffer,
                requested,
                &readCount,
                nullptr) ||
            readCount == 0)
        {
            return false;
        }
        hash.addData(
            QByteArray(
                buffer,
                static_cast<qsizetype>(readCount)));
        remaining -= readCount;
    }

    char extraByte = '\0';
    DWORD extraReadCount = 0;
    if (!ReadFile(
            handle,
            &extraByte,
            1,
            &extraReadCount,
            nullptr) ||
        extraReadCount != 0)
    {
        return false;
    }

    NativeFileIdentity afterIdentity;
    LARGE_INTEGER afterSize = {};
    if (!queryNativeFileIdentity(
            handle,
            &afterIdentity) ||
        !sameNativeFileIdentity(
            expectedIdentity,
            afterIdentity) ||
        !GetFileSizeEx(handle, &afterSize) ||
        afterSize.QuadPart != beforeSize.QuadPart)
    {
        return false;
    }

    *contentSize =
        static_cast<qint64>(beforeSize.QuadPart);
    *contentDigest = hash.result();
    return true;
}

class PinnedPath
{
public:
    PinnedPath() = default;

    ~PinnedPath()
    {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }

    PinnedPath(const PinnedPath&) = delete;
    PinnedPath& operator=(const PinnedPath&) = delete;

    bool open(
        const QString& value,
        bool expectedDirectory,
        bool requireSingleLink,
        bool requireContentRead = false)
    {
        path = value;
        directory = expectedDirectory;
        singleLinkRequired = requireSingleLink;
        contentReadRequired = requireContentRead;
        const DWORD flags =
            FILE_FLAG_OPEN_REPARSE_POINT |
            (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0);
        handle = CreateFileW(
            reinterpret_cast<LPCWSTR>(path.utf16()),
            FILE_READ_ATTRIBUTES |
                (contentReadRequired
                     ? GENERIC_READ
                     : 0),
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            flags,
            nullptr);
        if (!queryNativeFileIdentity(handle, &identity) ||
            identity.directory != directory ||
            (singleLinkRequired && identity.linkCount != 1))
        {
            return false;
        }
        if (contentReadRequired &&
            !digestOpenedRegularFile(
                handle,
                identity,
                &contentSize,
                &contentDigest))
        {
            return false;
        }
        return matchesCurrentPath();
    }

    bool matchesCurrentPath() const
    {
        if (handle == INVALID_HANDLE_VALUE)
            return false;
        const DWORD flags =
            FILE_FLAG_OPEN_REPARSE_POINT |
            (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0);
        const HANDLE currentHandle = CreateFileW(
            reinterpret_cast<LPCWSTR>(path.utf16()),
            FILE_READ_ATTRIBUTES |
                (contentReadRequired
                     ? GENERIC_READ
                     : 0),
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            flags,
            nullptr);
        NativeFileIdentity currentIdentity;
        bool matches =
            queryNativeFileIdentity(
                currentHandle,
                &currentIdentity) &&
            (!singleLinkRequired ||
             currentIdentity.linkCount == 1) &&
            sameNativeFileIdentity(
                identity,
                currentIdentity);
        if (matches && contentReadRequired)
        {
            qint64 currentContentSize = -1;
            QByteArray currentContentDigest;
            matches = digestOpenedRegularFile(
                          currentHandle,
                          currentIdentity,
                          &currentContentSize,
                          &currentContentDigest) &&
                currentContentSize == contentSize &&
                currentContentDigest == contentDigest;
        }
        if (currentHandle != INVALID_HANDLE_VALUE)
            CloseHandle(currentHandle);
        return matches;
    }

    QByteArray contentSha256() const
    {
        return contentDigest;
    }

private:
    HANDLE handle = INVALID_HANDLE_VALUE;
    NativeFileIdentity identity;
    qint64 contentSize = -1;
    QByteArray contentDigest;
    QString path;
    bool directory = false;
    bool singleLinkRequired = false;
    bool contentReadRequired = false;
};

class ScopedProcessLaunchErrorMode
{
public:
    ScopedProcessLaunchErrorMode()
    {
        constexpr DWORD flags =
            SEM_FAILCRITICALERRORS |
            SEM_NOGPFAULTERRORBOX |
            SEM_NOOPENFILEERRORBOX;
        const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        setThreadErrorMode = kernel != nullptr
            ? reinterpret_cast<SetThreadErrorModeFunction>(
                GetProcAddress(kernel, "SetThreadErrorMode"))
            : nullptr;
        if (setThreadErrorMode != nullptr)
        {
            changed = setThreadErrorMode(
                flags,
                &previousMode) != FALSE;
        }
        else
        {
            previousMode = SetErrorMode(flags);
            changed = true;
        }
    }

    ~ScopedProcessLaunchErrorMode()
    {
        if (!changed)
            return;
        if (setThreadErrorMode != nullptr)
            setThreadErrorMode(previousMode, nullptr);
        else
            SetErrorMode(previousMode);
    }

private:
    using SetThreadErrorModeFunction =
        BOOL(WINAPI*)(DWORD, LPDWORD);
    SetThreadErrorModeFunction setThreadErrorMode = nullptr;
    DWORD previousMode = 0;
    bool changed = false;
};
#else
struct NativeFileIdentity
{
    dev_t device = 0;
    ino_t inode = 0;
    nlink_t linkCount = 0;
    mode_t mode = 0;
};

bool sameNativeFileIdentity(
    const NativeFileIdentity& left,
    const NativeFileIdentity& right)
{
    return left.device == right.device &&
        left.inode == right.inode &&
        left.linkCount == right.linkCount &&
        ((left.mode & S_IFMT) == (right.mode & S_IFMT));
}

int pinnedPathOpenFlags(bool directory)
{
    int flags = O_CLOEXEC | O_NOFOLLOW;
    if (!directory)
        return flags | O_RDONLY;
#if defined(O_PATH)
    flags |= O_PATH;
#elif defined(O_EVTONLY)
    flags |= O_EVTONLY;
#else
    flags |= O_RDONLY;
#endif
#if defined(O_DIRECTORY)
    flags |= O_DIRECTORY;
#endif
    return flags;
}

bool digestOpenedRegularFile(
    int descriptor,
    const NativeFileIdentity& expectedIdentity,
    off_t* contentSize,
    QByteArray* contentDigest)
{
    struct stat before = {};
    if (descriptor < 0 ||
        ::fstat(descriptor, &before) != 0 ||
        !S_ISREG(before.st_mode) ||
        before.st_size < 0)
    {
        return false;
    }

    NativeFileIdentity beforeIdentity;
    beforeIdentity.device = before.st_dev;
    beforeIdentity.inode = before.st_ino;
    beforeIdentity.linkCount = before.st_nlink;
    beforeIdentity.mode = before.st_mode;
    if (!sameNativeFileIdentity(
            expectedIdentity,
            beforeIdentity))
    {
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    char buffer[64 * 1024];
    off_t offset = 0;
    while (offset < before.st_size)
    {
        const off_t remaining = before.st_size - offset;
        const std::size_t requested =
            remaining < static_cast<off_t>(sizeof(buffer))
            ? static_cast<std::size_t>(remaining)
            : sizeof(buffer);
        ssize_t readCount = -1;
        do
        {
            readCount = ::pread(
                descriptor,
                buffer,
                requested,
                offset);
        }
        while (readCount < 0 && errno == EINTR);
        if (readCount <= 0)
            return false;
        hash.addData(
            QByteArray(
                buffer,
                static_cast<int>(readCount)));
        offset += static_cast<off_t>(readCount);
    }

    char extraByte = '\0';
    ssize_t extraReadCount = -1;
    do
    {
        extraReadCount = ::pread(
            descriptor,
            &extraByte,
            1,
            before.st_size);
    }
    while (extraReadCount < 0 && errno == EINTR);
    if (extraReadCount != 0)
        return false;

    struct stat after = {};
    if (::fstat(descriptor, &after) != 0 ||
        after.st_size != before.st_size)
    {
        return false;
    }
    NativeFileIdentity afterIdentity;
    afterIdentity.device = after.st_dev;
    afterIdentity.inode = after.st_ino;
    afterIdentity.linkCount = after.st_nlink;
    afterIdentity.mode = after.st_mode;
    if (!sameNativeFileIdentity(
            expectedIdentity,
            afterIdentity))
    {
        return false;
    }

    *contentSize = before.st_size;
    *contentDigest = hash.result();
    return true;
}

class PinnedPath
{
public:
    PinnedPath() = default;

    ~PinnedPath()
    {
        if (descriptor >= 0)
            ::close(descriptor);
    }

    PinnedPath(const PinnedPath&) = delete;
    PinnedPath& operator=(const PinnedPath&) = delete;

    bool open(
        const QString& value,
        bool expectedDirectory,
        bool requireSingleLink,
        bool requireContentRead = false)
    {
        Q_UNUSED(requireContentRead);
        path = value;
        directory = expectedDirectory;
        singleLinkRequired = requireSingleLink;
        descriptor = ::open(
            QFile::encodeName(path).constData(),
            pinnedPathOpenFlags(directory));
        struct stat information = {};
        if (descriptor < 0 ||
            ::fstat(descriptor, &information) != 0 ||
            (directory && !S_ISDIR(information.st_mode)) ||
            (!directory && !S_ISREG(information.st_mode)) ||
            (singleLinkRequired && information.st_nlink != 1))
        {
            return false;
        }
        identity.device = information.st_dev;
        identity.inode = information.st_ino;
        identity.linkCount = information.st_nlink;
        identity.mode = information.st_mode;
        if (!directory &&
            !digestOpenedRegularFile(
                descriptor,
                identity,
                &contentSize,
                &contentDigest))
        {
            return false;
        }
        return matchesCurrentPath();
    }

    bool matchesCurrentPath() const
    {
        if (descriptor < 0)
            return false;
        const int currentDescriptor = ::open(
            QFile::encodeName(path).constData(),
            pinnedPathOpenFlags(directory));
        struct stat information = {};
        if (currentDescriptor < 0 ||
            ::fstat(currentDescriptor, &information) != 0 ||
            (directory && !S_ISDIR(information.st_mode)) ||
            (!directory && !S_ISREG(information.st_mode)) ||
            (singleLinkRequired && information.st_nlink != 1))
        {
            if (currentDescriptor >= 0)
                ::close(currentDescriptor);
            return false;
        }
        NativeFileIdentity currentIdentity;
        currentIdentity.device = information.st_dev;
        currentIdentity.inode = information.st_ino;
        currentIdentity.linkCount = information.st_nlink;
        currentIdentity.mode = information.st_mode;
        bool matches = sameNativeFileIdentity(
            identity,
            currentIdentity);
        if (matches && !directory)
        {
            off_t currentContentSize = -1;
            QByteArray currentContentDigest;
            matches = digestOpenedRegularFile(
                          currentDescriptor,
                          currentIdentity,
                          &currentContentSize,
                          &currentContentDigest) &&
                currentContentSize == contentSize &&
                currentContentDigest == contentDigest;
        }
        ::close(currentDescriptor);
        return matches;
    }

    QByteArray contentSha256() const
    {
        return contentDigest;
    }

private:
    int descriptor = -1;
    NativeFileIdentity identity;
    off_t contentSize = -1;
    QByteArray contentDigest;
    QString path;
    bool directory = false;
    bool singleLinkRequired = false;
};
#endif
}

struct DesktopRunController::PinnedLaunchTargets
{
    bool open(
        const QString& executablePath,
        const QString& descriptorPath,
        const QString& workingDirectory,
        bool requireDescriptorContent)
    {
        return executable.open(
                   executablePath,
                   false,
                   true,
                   false) &&
            descriptor.open(
                descriptorPath,
                false,
                true,
                requireDescriptorContent) &&
            directory.open(
                workingDirectory,
                true,
                false,
                false);
    }

    bool matchesCurrentPaths() const
    {
        return directory.matchesCurrentPath() &&
            executable.matchesCurrentPath() &&
            descriptor.matchesCurrentPath();
    }

    QByteArray descriptorContentSha256() const
    {
        return descriptor.contentSha256();
    }

    PinnedPath executable;
    PinnedPath descriptor;
    PinnedPath directory;
};

DesktopRunController::DesktopRunController(QObject* parent)
    : QObject(parent),
      process(new QProcess())
{
    process->setProcessChannelMode(
        QProcess::SeparateChannels);
    stopTimer.setSingleShot(true);
    stopTimer.setInterval(3000);

    connect(
        process,
        &QProcess::started,
        this,
        [this]()
        {
            if (currentState == DesktopRunState::Starting)
            {
                setState(DesktopRunState::Running);
            }
            else if (currentState == DesktopRunState::Stopping)
            {
                if (processWasKilled)
                {
                    process->kill();
                }
                else
                {
                    process->terminate();
                    stopTimer.start();
                }
            }
        });
    connect(
        process,
        &QProcess::readyReadStandardOutput,
        this,
        &DesktopRunController::drainStandardOutput);
    connect(
        process,
        &QProcess::readyReadStandardError,
        this,
        &DesktopRunController::drainStandardError);
    connect(
        process,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError error)
        {
            currentError = process->errorString();
            if (error == QProcess::FailedToStart)
            {
                finishRun(
                    DesktopRunOutcome::FailedToStart,
                    -1);
            }
        });
    connect(
        process,
        qOverload<int, QProcess::ExitStatus>(
            &QProcess::finished),
        this,
        [this](int processExitCode, QProcess::ExitStatus exitStatus)
        {
            const DesktopRunOutcome finalOutcome =
                userStopRequested
                ? DesktopRunOutcome::StoppedByUser
                : exitStatus == QProcess::CrashExit
                    ? DesktopRunOutcome::Crashed
                    : processExitCode == 0
                        ? DesktopRunOutcome::Succeeded
                        : DesktopRunOutcome::NonZeroExit;
            finishRun(finalOutcome, processExitCode);
        });
    connect(
        &stopTimer,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (currentState != DesktopRunState::Stopping)
                return;
            if (process->state() == QProcess::NotRunning)
                return;
            processWasKilled = true;
            process->kill();
        });
}

DesktopRunController::~DesktopRunController()
{
    stopTimer.stop();
    // QProcess destruction may synchronously wait for a child process. Keep
    // the unparented process object alive until its finished notification so
    // closing the editor never waits for game termination.
    QProcess* detachedProcess = process;
    process = nullptr;
    disconnect(
        detachedProcess,
        nullptr,
        this,
        nullptr);
    if (detachedProcess->state() !=
        QProcess::NotRunning)
    {
        connect(
            detachedProcess,
            qOverload<int, QProcess::ExitStatus>(
                &QProcess::finished),
            detachedProcess,
            &QObject::deleteLater);
        detachedProcess->kill();
    }
    else
    {
        delete detachedProcess;
    }
    pinnedLaunchTargets.reset();
}

DesktopRunState DesktopRunController::state() const
{
    return currentState;
}

DesktopRunOutcome DesktopRunController::outcome() const
{
    return currentOutcome;
}

int DesktopRunController::exitCode() const
{
    return currentExitCode;
}

bool DesktopRunController::forcedKill() const
{
    return processWasKilled;
}

QString DesktopRunController::lastError() const
{
    return currentError;
}

QString DesktopRunController::executablePath() const
{
    return currentExecutablePath;
}

QString DesktopRunController::descriptorPath() const
{
    return currentDescriptorPath;
}

bool DesktopRunController::isActive() const
{
    return currentState == DesktopRunState::Starting ||
        currentState == DesktopRunState::Running ||
        currentState == DesktopRunState::Stopping;
}

bool DesktopRunController::start(
    const QString& executablePath,
    const QString& descriptorPath,
    const QString& workingDirectory)
{
    return startInternal(
        executablePath,
        descriptorPath,
        workingDirectory,
        {});
}

bool DesktopRunController::start(
    const QString& executablePath,
    const DesktopRunSessionWorkspace& workspace,
    const QString& workingDirectory)
{
    currentError.clear();
    if (isActive())
    {
        currentError =
            QStringLiteral("a desktop game process is already active");
        return false;
    }
    if (!isLowercaseSha256(
            workspace.descriptorSha256))
    {
        currentError =
            QStringLiteral(
                "desktop-run workspace descriptor hash is invalid");
        return false;
    }
    if (!workspaceContractIsConsistent(
            workspace,
            workingDirectory))
    {
        currentError =
            QStringLiteral(
                "desktop-run workspace contract is inconsistent");
        return false;
    }

    const QString descriptorPath =
        workspace.paths.descriptorPath;
    const QByteArray expectedDescriptorSha256 =
        workspace.descriptorSha256;
    return startInternal(
        executablePath,
        descriptorPath,
        workingDirectory,
        expectedDescriptorSha256);
}

bool DesktopRunController::startInternal(
    const QString& executablePath,
    const QString& descriptorPath,
    const QString& workingDirectory,
    const QByteArray& expectedDescriptorSha256)
{
    currentError.clear();
    if (isActive())
    {
        currentError =
            QStringLiteral("a desktop game process is already active");
        return false;
    }

    const EditorSettings::DesktopExecutableValidation executableValidation =
        EditorSettings::validateDesktopGameExecutable(executablePath);
    if (!executableValidation.succeeded())
    {
        currentError =
            QStringLiteral("desktop game executable is invalid");
        return false;
    }
    const QString canonicalExecutablePath =
        canonicalUnlinkedPath(
            executableValidation.executablePath,
            false);
    if (canonicalExecutablePath.isEmpty() ||
        !hasSingleHardLink(canonicalExecutablePath))
    {
        currentError =
            QStringLiteral(
                "desktop game executable identity is invalid");
        return false;
    }
    const QString canonicalDescriptorPath =
        canonicalUnlinkedPath(descriptorPath, false);
    if (canonicalDescriptorPath.isEmpty() ||
        !hasSingleHardLink(canonicalDescriptorPath))
    {
        currentError =
            QStringLiteral("editor-run descriptor is invalid");
        return false;
    }
    const QString canonicalWorkingDirectory =
        canonicalUnlinkedPath(workingDirectory, true);
    if (canonicalWorkingDirectory.isEmpty())
    {
        currentError =
            QStringLiteral("desktop game working directory is invalid");
        return false;
    }
    const QString canonicalDescriptorParent =
        canonicalUnlinkedPath(
            QFileInfo(canonicalDescriptorPath)
                .absolutePath(),
            true);
    if (canonicalDescriptorParent.isEmpty() ||
        !sameLiteralPath(
            canonicalDescriptorParent,
            canonicalWorkingDirectory))
    {
        currentError =
            QStringLiteral(
                "editor-run descriptor is outside the session directory");
        return false;
    }
    auto launchTargets =
        std::make_unique<PinnedLaunchTargets>();
    if (!launchTargets->open(
            canonicalExecutablePath,
            canonicalDescriptorPath,
            canonicalWorkingDirectory,
            !expectedDescriptorSha256.isEmpty()))
    {
        currentError =
            QStringLiteral(
                "desktop launch targets could not be pinned");
        return false;
    }
    if (!expectedDescriptorSha256.isEmpty() &&
        launchTargets->descriptorContentSha256() !=
            QByteArray::fromHex(
                expectedDescriptorSha256))
    {
        currentError =
            QStringLiteral(
                "editor-run descriptor content does not match the workspace hash");
        return false;
    }
    const EditorSettings::DesktopExecutableValidation
        pinnedExecutableValidation =
            EditorSettings::validateDesktopGameExecutable(
                canonicalExecutablePath);
    if (!pinnedExecutableValidation.succeeded() ||
        !sameLiteralPath(
            pinnedExecutableValidation.executablePath,
            canonicalExecutablePath) ||
        !sameLiteralPath(
            canonicalUnlinkedPath(
                canonicalDescriptorPath,
                false),
            canonicalDescriptorPath) ||
        !sameLiteralPath(
            canonicalUnlinkedPath(
                canonicalWorkingDirectory,
                true),
            canonicalWorkingDirectory) ||
        !hasSingleHardLink(canonicalExecutablePath) ||
        !hasSingleHardLink(canonicalDescriptorPath) ||
        !launchTargets->matchesCurrentPaths())
    {
        currentError =
            QStringLiteral(
                "desktop launch targets changed while being pinned");
        return false;
    }

    stopTimer.stop();
    standardOutputDecoder =
        QStringDecoder(QStringDecoder::Utf8);
    standardErrorDecoder =
        QStringDecoder(QStringDecoder::Utf8);
    currentOutcome = DesktopRunOutcome::None;
    currentExitCode = 0;
    userStopRequested = false;
    processWasKilled = false;
    currentExecutablePath =
        canonicalExecutablePath;
    currentDescriptorPath =
        canonicalDescriptorPath;
    pinnedLaunchTargets = std::move(launchTargets);

    process->setProgram(currentExecutablePath);
    process->setArguments({
        QStringLiteral("--editor-run"),
        currentDescriptorPath
    });
    process->setWorkingDirectory(
        canonicalWorkingDirectory);
    const std::uint64_t launchGeneration =
        ++currentRunGeneration;
    setState(DesktopRunState::Starting);
    if (currentRunGeneration != launchGeneration)
        return false;
    if (currentState != DesktopRunState::Starting ||
        userStopRequested)
    {
        finishRun(
            DesktopRunOutcome::StoppedByUser,
            -1);
        return false;
    }
    const QString currentCanonicalExecutablePath =
        canonicalUnlinkedPath(
            currentExecutablePath,
            false);
    const QString currentCanonicalDescriptorPath =
        canonicalUnlinkedPath(
            currentDescriptorPath,
            false);
    const QString currentCanonicalWorkingDirectory =
        canonicalUnlinkedPath(
            canonicalWorkingDirectory,
            true);
    if (!pinnedLaunchTargets ||
        !sameLiteralPath(
            currentCanonicalExecutablePath,
            currentExecutablePath) ||
        !sameLiteralPath(
            currentCanonicalDescriptorPath,
            currentDescriptorPath) ||
        !sameLiteralPath(
            currentCanonicalWorkingDirectory,
            canonicalWorkingDirectory) ||
        !hasSingleHardLink(currentExecutablePath) ||
        !hasSingleHardLink(currentDescriptorPath) ||
        !pinnedLaunchTargets->matchesCurrentPaths())
    {
        currentError =
            QStringLiteral(
                "desktop launch target identity changed before process start");
        finishRun(
            DesktopRunOutcome::FailedToStart,
            -1);
        return false;
    }
#ifdef Q_OS_WIN
    const ScopedProcessLaunchErrorMode errorMode;
#endif
    process->start(QIODevice::ReadOnly);
    return true;
}

void DesktopRunController::requestStop()
{
    if (!isActive() || currentState == DesktopRunState::Stopping)
        return;
    userStopRequested = true;
    setState(DesktopRunState::Stopping);
    stopTimer.start();
    process->terminate();
}

bool DesktopRunController::stopImmediatelyForApplicationExit()
{
    stopTimer.stop();
    if (process == nullptr ||
        process->state() == QProcess::NotRunning)
    {
        return true;
    }
    userStopRequested = true;
    processWasKilled = true;
    process->kill();
    return process->waitForFinished(3000) &&
        process->state() == QProcess::NotRunning;
}

void DesktopRunController::setStopTimeoutMilliseconds(
    int milliseconds)
{
    stopTimer.setInterval(milliseconds > 0 ? milliseconds : 1);
}

void DesktopRunController::setState(DesktopRunState state)
{
    if (currentState == state)
        return;
    currentState = state;
    emit stateChanged(currentState);
}

void DesktopRunController::drainStandardOutput()
{
    const QByteArray bytes =
        process->readAllStandardOutput();
    if (bytes.isEmpty())
        return;
    const QString text = standardOutputDecoder(bytes);
    if (!text.isEmpty())
        emit standardOutputReceived(text);
}

void DesktopRunController::drainStandardError()
{
    const QByteArray bytes =
        process->readAllStandardError();
    if (bytes.isEmpty())
        return;
    const QString text = standardErrorDecoder(bytes);
    if (!text.isEmpty())
        emit standardErrorReceived(text);
}

void DesktopRunController::finishDecoder(
    QStringDecoder& decoder,
    bool standardError)
{
    const QString text = finalizedDecoderText(decoder);
    if (text.isEmpty())
        return;
    if (standardError)
        emit standardErrorReceived(text);
    else
        emit standardOutputReceived(text);
}

void DesktopRunController::finishRun(
    DesktopRunOutcome outcome,
    int exitCode)
{
    if (currentState == DesktopRunState::Finished)
        return;
    stopTimer.stop();
    drainStandardOutput();
    drainStandardError();
    finishDecoder(standardOutputDecoder, false);
    finishDecoder(standardErrorDecoder, true);
    currentOutcome = outcome;
    currentExitCode = exitCode;
    const DesktopRunOutcome finishedOutcome =
        currentOutcome;
    const int finishedExitCode = currentExitCode;
    const bool finishedForcedKill = processWasKilled;
    pinnedLaunchTargets.reset();
    setState(DesktopRunState::Finished);
    emit runFinished(
        finishedOutcome,
        finishedExitCode,
        finishedForcedKill);
}
