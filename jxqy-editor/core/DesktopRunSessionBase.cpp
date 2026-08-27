#include "DesktopRunSessionBase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <aclapi.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "Advapi32.lib")
#endif
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace
{
constexpr auto BaseDirectoryName =
    "desktop-run-sessions";

bool exactUtf8(
    const QString& value,
    QByteArray& encoded)
{
    if (value.contains(QChar(u'\0')))
        return false;
    encoded = value.toUtf8();
    return QString::fromUtf8(encoded) == value;
}

QString normalizedAbsolutePath(
    const QString& path)
{
    return QDir::cleanPath(
        QFileInfo(path).absoluteFilePath());
}

void fail(
    DesktopRunSessionBaseResult& result,
    DesktopRunSessionBaseError error,
    const QString& problemPath,
    const QString& message)
{
    result.error = error;
    result.problemPath = problemPath;
    result.message = message;
}

bool validBaseDirectoryName(
    const QString& name)
{
    QByteArray encoded;
    const bool generallyValid =
        !name.isEmpty() &&
        name != QStringLiteral(".") &&
        name != QStringLiteral("..") &&
        !QDir::isAbsolutePath(name) &&
        !name.contains(QLatin1Char('/')) &&
        !name.contains(QLatin1Char('\\')) &&
        exactUtf8(name, encoded);
    if (!generallyValid)
        return false;
#ifdef Q_OS_WIN
    if (name.endsWith(QLatin1Char('.')) ||
        name.endsWith(QLatin1Char(' ')) ||
        name.contains(
            QRegularExpression(
                QStringLiteral(
                    R"([<>:"|?*])"))))
    {
        return false;
    }
    const QString deviceStem =
        name.section(
                QLatin1Char('.'), 0, 0)
            .toUpper();
    if (deviceStem ==
            QStringLiteral("CON") ||
        deviceStem ==
            QStringLiteral("PRN") ||
        deviceStem ==
            QStringLiteral("AUX") ||
        deviceStem ==
            QStringLiteral("NUL"))
    {
        return false;
    }
    if ((deviceStem.startsWith(
             QStringLiteral("COM")) ||
         deviceStem.startsWith(
             QStringLiteral("LPT"))) &&
        deviceStem.size() == 4 &&
        deviceStem.at(3) >=
            QLatin1Char('1') &&
        deviceStem.at(3) <=
            QLatin1Char('9'))
    {
        return false;
    }
#endif
    return true;
}

#ifdef Q_OS_WIN
class WindowsHandle
{
public:
    WindowsHandle() = default;
    explicit WindowsHandle(HANDLE value)
        : m_value(value)
    {
    }
    ~WindowsHandle()
    {
        reset();
    }

    WindowsHandle(const WindowsHandle&) = delete;
    WindowsHandle& operator=(
        const WindowsHandle&) = delete;

    WindowsHandle(WindowsHandle&& other) noexcept
        : m_value(other.release())
    {
    }
    WindowsHandle& operator=(
        WindowsHandle&& other) noexcept
    {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    bool valid() const
    {
        return m_value != INVALID_HANDLE_VALUE;
    }
    HANDLE get() const
    {
        return m_value;
    }
    HANDLE release()
    {
        const HANDLE value = m_value;
        m_value = INVALID_HANDLE_VALUE;
        return value;
    }
    void reset(
        HANDLE value = INVALID_HANDLE_VALUE)
    {
        if (valid())
            CloseHandle(m_value);
        m_value = value;
    }

private:
    HANDLE m_value = INVALID_HANDLE_VALUE;
};

class LocalMemory
{
public:
    LocalMemory() = default;
    explicit LocalMemory(void* value)
        : m_value(value)
    {
    }
    ~LocalMemory()
    {
        reset();
    }

    LocalMemory(const LocalMemory&) = delete;
    LocalMemory& operator=(
        const LocalMemory&) = delete;

    void* get() const
    {
        return m_value;
    }
    void reset(void* value = nullptr)
    {
        if (m_value != nullptr)
            LocalFree(m_value);
        m_value = value;
    }

private:
    void* m_value = nullptr;
};

struct CurrentWindowsUser
{
    WindowsHandle token;
    std::vector<unsigned char> storage;
    PSID sid = nullptr;
};

bool loadCurrentWindowsUser(
    CurrentWindowsUser& user,
    QString& message)
{
    HANDLE token = INVALID_HANDLE_VALUE;
    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &token))
    {
        message = QStringLiteral(
            "Cannot open the current Windows user token "
            "(system error %1)")
            .arg(GetLastError());
        return false;
    }
    user.token.reset(token);

    DWORD requiredBytes = 0;
    if (GetTokenInformation(
            user.token.get(),
            TokenUser,
            nullptr,
            0,
            &requiredBytes) ||
        GetLastError() !=
            ERROR_INSUFFICIENT_BUFFER ||
        requiredBytes == 0)
    {
        message = QStringLiteral(
            "Cannot size the current Windows user SID "
            "(system error %1)")
            .arg(GetLastError());
        return false;
    }
    user.storage.resize(requiredBytes);
    if (!GetTokenInformation(
            user.token.get(),
            TokenUser,
            user.storage.data(),
            requiredBytes,
            &requiredBytes))
    {
        message = QStringLiteral(
            "Cannot read the current Windows user SID "
            "(system error %1)")
            .arg(GetLastError());
        return false;
    }
    user.sid =
        reinterpret_cast<TOKEN_USER*>(
            user.storage.data())->User.Sid;
    if (user.sid == nullptr ||
        !IsValidSid(user.sid))
    {
        message = QStringLiteral(
            "The current Windows user SID is invalid");
        return false;
    }
    return true;
}

bool createWellKnownSid(
    WELL_KNOWN_SID_TYPE type,
    std::vector<unsigned char>& storage,
    PSID& sid,
    QString& message)
{
    storage.resize(SECURITY_MAX_SID_SIZE);
    DWORD bytes =
        static_cast<DWORD>(storage.size());
    if (!CreateWellKnownSid(
            type,
            nullptr,
            storage.data(),
            &bytes))
    {
        message = QStringLiteral(
            "Cannot create a trusted Windows SID "
            "(system error %1)")
            .arg(GetLastError());
        return false;
    }
    storage.resize(bytes);
    sid = storage.data();
    return true;
}

bool createPrivateWindowsDirectory(
    const QString& path,
    bool& alreadyExists,
    QString& message)
{
    alreadyExists = false;
    CurrentWindowsUser currentUser;
    if (!loadCurrentWindowsUser(
            currentUser, message))
    {
        return false;
    }

    std::vector<unsigned char> systemStorage;
    std::vector<unsigned char>
        administratorsStorage;
    PSID systemSid = nullptr;
    PSID administratorsSid = nullptr;
    if (!createWellKnownSid(
            WinLocalSystemSid,
            systemStorage,
            systemSid,
            message) ||
        !createWellKnownSid(
            WinBuiltinAdministratorsSid,
            administratorsStorage,
            administratorsSid,
            message))
    {
        return false;
    }

    EXPLICIT_ACCESSW access[3] = {};
    const auto configureAccess =
        [](EXPLICIT_ACCESSW& entry,
           PSID sid,
           TRUSTEE_TYPE trusteeType)
        {
            entry.grfAccessPermissions =
                FILE_ALL_ACCESS;
            entry.grfAccessMode = SET_ACCESS;
            entry.grfInheritance =
                SUB_CONTAINERS_AND_OBJECTS_INHERIT;
            entry.Trustee.TrusteeForm =
                TRUSTEE_IS_SID;
            entry.Trustee.TrusteeType =
                trusteeType;
            entry.Trustee.ptstrName =
                static_cast<LPWSTR>(sid);
        };
    configureAccess(
        access[0],
        currentUser.sid,
        TRUSTEE_IS_USER);
    configureAccess(
        access[1],
        systemSid,
        TRUSTEE_IS_WELL_KNOWN_GROUP);
    configureAccess(
        access[2],
        administratorsSid,
        TRUSTEE_IS_GROUP);

    PACL rawDacl = nullptr;
    const DWORD aclResult =
        SetEntriesInAclW(
            3,
            access,
            nullptr,
            &rawDacl);
    LocalMemory dacl(rawDacl);
    if (aclResult != ERROR_SUCCESS ||
        rawDacl == nullptr)
    {
        message = QStringLiteral(
            "Cannot build the private Windows sessions-base DACL "
            "(system error %1)")
            .arg(aclResult);
        return false;
    }

    SECURITY_DESCRIPTOR descriptor = {};
    if (!InitializeSecurityDescriptor(
            &descriptor,
            SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorOwner(
            &descriptor,
            currentUser.sid,
            FALSE) ||
        !SetSecurityDescriptorDacl(
            &descriptor,
            TRUE,
            rawDacl,
            FALSE) ||
        !SetSecurityDescriptorControl(
            &descriptor,
            SE_DACL_PROTECTED,
            SE_DACL_PROTECTED))
    {
        message = QStringLiteral(
            "Cannot build the private Windows sessions-base security descriptor "
            "(system error %1)")
            .arg(GetLastError());
        return false;
    }

    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength =
        sizeof(securityAttributes);
    securityAttributes.lpSecurityDescriptor =
        &descriptor;
    securityAttributes.bInheritHandle = FALSE;

    const std::wstring nativePath =
        QDir::toNativeSeparators(path)
            .toStdWString();
    if (CreateDirectoryW(
            nativePath.c_str(),
            &securityAttributes))
    {
        return true;
    }

    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS ||
        error == ERROR_FILE_EXISTS)
    {
        alreadyExists = true;
        return true;
    }
    message = QStringLiteral(
        "Cannot atomically create the private Windows sessions base "
        "(system error %1)")
        .arg(error);
    return false;
}

bool openWindowsDirectoryWithoutFollowing(
    const QString& path,
    bool writable,
    WindowsHandle& handle,
    DesktopRunSessionBaseError& error,
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
    if (writable)
    {
        access |=
            FILE_ADD_SUBDIRECTORY |
            FILE_WRITE_ATTRIBUTES;
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
        const DWORD systemError = GetLastError();
        if (systemError ==
                ERROR_FILE_NOT_FOUND ||
            systemError ==
                ERROR_PATH_NOT_FOUND)
        {
            error =
                DesktopRunSessionBaseError::
                    ParentUnavailable;
        }
        message = QStringLiteral(
            "Cannot open the directory without following links "
            "(system error %1)")
            .arg(systemError);
        return false;
    }
    handle.reset(value);

    FILE_ATTRIBUTE_TAG_INFO attributes = {};
    if (!GetFileInformationByHandleEx(
            handle.get(),
            FileAttributeTagInfo,
            &attributes,
            sizeof(attributes)))
    {
        message = QStringLiteral(
            "Cannot inspect directory attributes "
            "(system error %1)")
            .arg(GetLastError());
        return false;
    }
    if ((attributes.FileAttributes &
         FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        error =
            DesktopRunSessionBaseError::
                ParentIsLink;
        message = QStringLiteral(
            "Directory must not be a symbolic link, junction, or other reparse point");
        return false;
    }
    if ((attributes.FileAttributes &
         FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        error =
            DesktopRunSessionBaseError::
                ParentIsNotDirectory;
        message = QStringLiteral(
            "Path is not a directory");
        return false;
    }
    return true;
}

bool sameWindowsFileIdentity(
    HANDLE left,
    HANDLE right)
{
    BY_HANDLE_FILE_INFORMATION leftInformation = {};
    BY_HANDLE_FILE_INFORMATION rightInformation = {};
    return GetFileInformationByHandle(
               left,
               &leftInformation) &&
        GetFileInformationByHandle(
            right,
            &rightInformation) &&
        leftInformation.dwVolumeSerialNumber ==
            rightInformation.dwVolumeSerialNumber &&
        leftInformation.nFileIndexHigh ==
            rightInformation.nFileIndexHigh &&
        leftInformation.nFileIndexLow ==
            rightInformation.nFileIndexLow;
}

bool trustedWindowsDirectorySecurity(
    HANDLE handle,
    DesktopRunSessionBaseError& error,
    QString& message)
{
    CurrentWindowsUser currentUser;
    if (!loadCurrentWindowsUser(
            currentUser, message))
    {
        error =
            DesktopRunSessionBaseError::
                BaseOwnerMismatch;
        return false;
    }

    PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    const DWORD securityResult =
        GetSecurityInfo(
            handle,
            SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION |
                DACL_SECURITY_INFORMATION,
            &owner,
            nullptr,
            &dacl,
            nullptr,
            &rawDescriptor);
    LocalMemory descriptor(rawDescriptor);
    if (securityResult != ERROR_SUCCESS)
    {
        error =
            DesktopRunSessionBaseError::
                BasePermissionsUntrusted;
        message = QStringLiteral(
            "Cannot inspect the sessions-base owner and DACL "
            "(system error %1)")
            .arg(securityResult);
        return false;
    }
    if (owner == nullptr ||
        !EqualSid(owner, currentUser.sid))
    {
        error =
            DesktopRunSessionBaseError::
                BaseOwnerMismatch;
        message = QStringLiteral(
            "Sessions base is not owned by the current Windows user");
        return false;
    }
    if (dacl == nullptr)
    {
        error =
            DesktopRunSessionBaseError::
                BasePermissionsUntrusted;
        message = QStringLiteral(
            "Sessions base has no explicit access-control list");
        return false;
    }

    constexpr ACCESS_MASK SensitiveAccess =
        FILE_LIST_DIRECTORY |
        FILE_ADD_FILE |
        FILE_ADD_SUBDIRECTORY |
        FILE_READ_EA |
        FILE_WRITE_EA |
        FILE_TRAVERSE |
        FILE_DELETE_CHILD |
        FILE_READ_ATTRIBUTES |
        FILE_WRITE_ATTRIBUTES |
        DELETE |
        READ_CONTROL |
        WRITE_DAC |
        WRITE_OWNER |
        GENERIC_READ |
        GENERIC_WRITE |
        GENERIC_EXECUTE |
        GENERIC_ALL;

    for (DWORD index = 0;
         index < dacl->AceCount;
         ++index)
    {
        void* rawAce = nullptr;
        if (!GetAce(
                dacl, index, &rawAce))
        {
            error =
                DesktopRunSessionBaseError::
                    BasePermissionsUntrusted;
            message = QStringLiteral(
                "Cannot inspect a sessions-base access-control entry");
            return false;
        }

        const auto* header =
            static_cast<ACE_HEADER*>(rawAce);
        if (header->AceType ==
                ACCESS_DENIED_ACE_TYPE ||
            header->AceType ==
                SYSTEM_AUDIT_ACE_TYPE)
        {
            continue;
        }
        if (header->AceType !=
                ACCESS_ALLOWED_ACE_TYPE)
        {
            error =
                DesktopRunSessionBaseError::
                    BasePermissionsUntrusted;
            message = QStringLiteral(
                "Sessions base contains an unsupported access-allow entry");
            return false;
        }

        const auto* ace =
            static_cast<
                ACCESS_ALLOWED_ACE*>(
                    rawAce);
        if ((ace->Mask &
             SensitiveAccess) == 0)
        {
            continue;
        }
        const PSID sid =
            const_cast<DWORD*>(
                &ace->SidStart);
        if (!IsValidSid(sid))
        {
            error =
                DesktopRunSessionBaseError::
                    BasePermissionsUntrusted;
            message = QStringLiteral(
                "Sessions base contains an invalid access-control SID");
            return false;
        }

        const bool trusted =
            EqualSid(sid, currentUser.sid) ||
            IsWellKnownSid(
                sid, WinLocalSystemSid) ||
            IsWellKnownSid(
                sid,
                WinBuiltinAdministratorsSid) ||
            IsWellKnownSid(
                sid, WinCreatorOwnerSid);
        if (!trusted)
        {
            error =
                DesktopRunSessionBaseError::
                    BasePermissionsUntrusted;
            message = QStringLiteral(
                "Sessions base grants sensitive access to an untrusted Windows SID");
            return false;
        }
    }
    return true;
}

DesktopRunSessionBaseResult
ensureDesktopRunSessionBaseWindows(
    const QString& parentPath,
    const QString& baseName,
    const QString& basePath)
{
    DesktopRunSessionBaseResult result;
    result.path = basePath;

    DesktopRunSessionBaseError nativeError =
        DesktopRunSessionBaseError::
            ParentUnavailable;
    QString message;
    WindowsHandle parent;
    if (!openWindowsDirectoryWithoutFollowing(
            parentPath,
            true,
            parent,
            nativeError,
            message))
    {
        fail(
            result,
            nativeError,
            parentPath,
            message);
        return result;
    }

    const QString canonicalParent =
        QFileInfo(parentPath)
            .canonicalFilePath();
    if (canonicalParent.isEmpty())
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                ParentUnavailable,
            parentPath,
            QStringLiteral(
                "Parent directory cannot be canonicalized"));
        return result;
    }
    const QString canonicalCandidate =
        QDir(
            QDir::cleanPath(
                canonicalParent))
            .filePath(baseName);

    bool alreadyExists = false;
    if (!createPrivateWindowsDirectory(
            canonicalCandidate,
            alreadyExists,
            message))
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseCreationFailed,
            canonicalCandidate,
            message);
        return result;
    }
    result.created = !alreadyExists;

    WindowsHandle base;
    nativeError =
        DesktopRunSessionBaseError::
            BaseCreationFailed;
    if (!openWindowsDirectoryWithoutFollowing(
            canonicalCandidate,
            false,
            base,
            nativeError,
            message))
    {
        if (nativeError ==
                DesktopRunSessionBaseError::
                    ParentIsLink)
        {
            nativeError =
                DesktopRunSessionBaseError::
                    BaseIsLink;
        }
        else if (nativeError ==
                 DesktopRunSessionBaseError::
                     ParentIsNotDirectory)
        {
            nativeError =
                DesktopRunSessionBaseError::
                    BaseIsNotDirectory;
        }
        fail(
            result,
            nativeError,
            canonicalCandidate,
            message);
        return result;
    }

    if (!trustedWindowsDirectorySecurity(
            base.get(),
            nativeError,
            message))
    {
        fail(
            result,
            nativeError,
            canonicalCandidate,
            message);
        return result;
    }

    WindowsHandle routedParent;
    DesktopRunSessionBaseError routeError =
        DesktopRunSessionBaseError::
            BaseRouteChanged;
    if (!openWindowsDirectoryWithoutFollowing(
            QDir::cleanPath(
                canonicalParent),
            false,
            routedParent,
            routeError,
            message) ||
        !sameWindowsFileIdentity(
            parent.get(),
            routedParent.get()))
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseRouteChanged,
            canonicalCandidate,
            QStringLiteral(
                "Sessions-base parent route changed while the base was being validated"));
        return result;
    }

    WindowsHandle routedBase;
    routeError =
        DesktopRunSessionBaseError::
            BaseRouteChanged;
    if (!openWindowsDirectoryWithoutFollowing(
            canonicalCandidate,
            false,
            routedBase,
            routeError,
            message) ||
        !sameWindowsFileIdentity(
            base.get(),
            routedBase.get()))
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseRouteChanged,
            canonicalCandidate,
            QStringLiteral(
                "Sessions-base route changed while the base was being validated"));
        return result;
    }

    const QString canonicalBase =
        QFileInfo(canonicalCandidate)
            .canonicalFilePath();
    if (canonicalBase.isEmpty() ||
        QFileInfo(canonicalBase)
                .canonicalPath()
                .compare(
                    QDir::cleanPath(
                        canonicalParent),
                    Qt::CaseInsensitive) != 0)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseRouteChanged,
            canonicalCandidate,
            QStringLiteral(
                "Sessions-base route changed while it was being validated"));
        return result;
    }

    result.path =
        QDir::cleanPath(canonicalBase);
    result.error =
        DesktopRunSessionBaseError::None;
    result.problemPath.clear();
    result.message.clear();
    return result;
}
#else
class PosixDescriptor
{
public:
    PosixDescriptor() = default;
    explicit PosixDescriptor(int value)
        : m_value(value)
    {
    }
    ~PosixDescriptor()
    {
        reset();
    }

    PosixDescriptor(const PosixDescriptor&) = delete;
    PosixDescriptor& operator=(
        const PosixDescriptor&) = delete;

    bool valid() const
    {
        return m_value >= 0;
    }
    int get() const
    {
        return m_value;
    }
    int release()
    {
        const int value = m_value;
        m_value = -1;
        return value;
    }
    void reset(int value = -1)
    {
        if (valid())
            ::close(m_value);
        m_value = value;
    }

private:
    int m_value = -1;
};

DesktopRunSessionBaseResult
ensureDesktopRunSessionBasePosix(
    const QString& parentPath,
    const QString& baseName,
    const QString& basePath)
{
    DesktopRunSessionBaseResult result;
    result.path = basePath;

    const QByteArray parentNative =
        QFile::encodeName(parentPath);
    struct stat parentBefore = {};
    if (::lstat(
            parentNative.constData(),
            &parentBefore) != 0)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                ParentUnavailable,
            parentPath,
            QStringLiteral(
                "Cannot inspect the sessions-base parent "
                "(system error %1)")
                .arg(errno));
        return result;
    }
    if (S_ISLNK(parentBefore.st_mode))
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                ParentIsLink,
            parentPath,
            QStringLiteral(
                "Sessions-base parent must not be a symbolic link"));
        return result;
    }
    if (!S_ISDIR(parentBefore.st_mode))
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                ParentIsNotDirectory,
            parentPath,
            QStringLiteral(
                "Sessions-base parent is not a directory"));
        return result;
    }

    PosixDescriptor parent(
        ::open(
            parentNative.constData(),
            O_RDONLY |
                O_DIRECTORY |
                O_CLOEXEC |
                O_NOFOLLOW));
    if (!parent.valid())
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                ParentUnavailable,
            parentPath,
            QStringLiteral(
                "Cannot open the sessions-base parent without following links "
                "(system error %1)")
                .arg(errno));
        return result;
    }
    struct stat parentAfter = {};
    if (::fstat(
            parent.get(),
            &parentAfter) != 0 ||
        parentBefore.st_dev !=
            parentAfter.st_dev ||
        parentBefore.st_ino !=
            parentAfter.st_ino)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseRouteChanged,
            parentPath,
            QStringLiteral(
                "Sessions-base parent changed while it was being opened"));
        return result;
    }

    const QString canonicalParent =
        QFileInfo(parentPath)
            .canonicalFilePath();
    if (canonicalParent.isEmpty())
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                ParentUnavailable,
            parentPath,
            QStringLiteral(
                "Sessions-base parent cannot be canonicalized"));
        return result;
    }
    const QString canonicalCandidate =
        QDir(
            QDir::cleanPath(
                canonicalParent))
            .filePath(baseName);
    const QByteArray baseNative =
        QFile::encodeName(baseName);

    bool created = false;
    if (::mkdirat(
            parent.get(),
            baseNative.constData(),
            0700) == 0)
    {
        created = true;
    }
    else if (errno != EEXIST)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseCreationFailed,
            canonicalCandidate,
            QStringLiteral(
                "Cannot create the private POSIX sessions base "
                "(system error %1)")
                .arg(errno));
        return result;
    }
    result.created = created;

    if (created &&
        ::fchmodat(
            parent.get(),
            baseNative.constData(),
            0700,
            0) != 0)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BasePermissionsUntrusted,
            canonicalCandidate,
            QStringLiteral(
                "Cannot apply exact mode 0700 to the newly created sessions base "
                "(system error %1)")
                .arg(errno));
        return result;
    }

    struct stat baseBefore = {};
    if (::fstatat(
            parent.get(),
            baseNative.constData(),
            &baseBefore,
            AT_SYMLINK_NOFOLLOW) != 0)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseCreationFailed,
            canonicalCandidate,
            QStringLiteral(
                "Cannot inspect the sessions base "
                "(system error %1)")
                .arg(errno));
        return result;
    }
    if (S_ISLNK(baseBefore.st_mode))
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseIsLink,
            canonicalCandidate,
            QStringLiteral(
                "Sessions base must not be a symbolic link"));
        return result;
    }
    if (!S_ISDIR(baseBefore.st_mode))
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseIsNotDirectory,
            canonicalCandidate,
            QStringLiteral(
                "Sessions-base path is not a directory"));
        return result;
    }

    PosixDescriptor base(
        ::openat(
            parent.get(),
            baseNative.constData(),
            O_RDONLY |
                O_DIRECTORY |
                O_CLOEXEC |
                O_NOFOLLOW));
    if (!base.valid())
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseCreationFailed,
            canonicalCandidate,
            QStringLiteral(
                "Cannot open the sessions base without following links "
                "(system error %1)")
                .arg(errno));
        return result;
    }

    struct stat baseAfter = {};
    if (::fstat(
            base.get(),
            &baseAfter) != 0 ||
        baseBefore.st_dev !=
            baseAfter.st_dev ||
        baseBefore.st_ino !=
            baseAfter.st_ino)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseRouteChanged,
            canonicalCandidate,
            QStringLiteral(
                "Sessions-base identity changed while it was being opened"));
        return result;
    }

    if (created &&
        ::fchmod(base.get(), 0700) != 0)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BasePermissionsUntrusted,
            canonicalCandidate,
            QStringLiteral(
                "Cannot apply exact mode 0700 to the newly created sessions base "
                "(system error %1)")
                .arg(errno));
        return result;
    }
    if (::fstat(
            base.get(),
            &baseAfter) != 0)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BasePermissionsUntrusted,
            canonicalCandidate,
            QStringLiteral(
                "Cannot inspect sessions-base ownership and permissions "
                "(system error %1)")
                .arg(errno));
        return result;
    }
    if (baseAfter.st_uid != ::geteuid())
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseOwnerMismatch,
            canonicalCandidate,
            QStringLiteral(
                "Sessions base is not owned by the current effective user"));
        return result;
    }
    if ((baseAfter.st_mode & 07777) !=
        0700)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BasePermissionsUntrusted,
            canonicalCandidate,
            QStringLiteral(
                "Sessions base must have exact POSIX mode 0700"));
        return result;
    }

    struct stat routedParent = {};
    struct stat routedBase = {};
    const QByteArray canonicalParentNative =
        QFile::encodeName(
            QDir::cleanPath(
                canonicalParent));
    const QByteArray canonicalBaseNative =
        QFile::encodeName(
            canonicalCandidate);
    if (::lstat(
            canonicalParentNative.constData(),
            &routedParent) != 0 ||
        ::lstat(
            canonicalBaseNative.constData(),
            &routedBase) != 0 ||
        !S_ISDIR(routedParent.st_mode) ||
        S_ISLNK(routedParent.st_mode) ||
        !S_ISDIR(routedBase.st_mode) ||
        S_ISLNK(routedBase.st_mode) ||
        routedParent.st_dev !=
            parentAfter.st_dev ||
        routedParent.st_ino !=
            parentAfter.st_ino ||
        routedBase.st_dev !=
            baseAfter.st_dev ||
        routedBase.st_ino !=
            baseAfter.st_ino)
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseRouteChanged,
            canonicalCandidate,
            QStringLiteral(
                "Sessions-base route changed while the base was being validated"));
        return result;
    }

    const QString canonicalBase =
        QFileInfo(canonicalCandidate)
            .canonicalFilePath();
    if (canonicalBase.isEmpty() ||
        QFileInfo(canonicalBase)
                .canonicalPath() !=
            QDir::cleanPath(canonicalParent))
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                BaseRouteChanged,
            canonicalCandidate,
            QStringLiteral(
                "Sessions-base route changed while it was being validated"));
        return result;
    }

    result.path =
        QDir::cleanPath(canonicalBase);
    result.error =
        DesktopRunSessionBaseError::None;
    result.problemPath.clear();
    result.message.clear();
    return result;
}
#endif
}

QString DesktopRunSessionBaseLocation::
baseDirectoryPath() const
{
    if (parentDirectory.isEmpty() ||
        baseDirectoryName.isEmpty())
    {
        return {};
    }
    return QDir(parentDirectory)
        .filePath(baseDirectoryName);
}

DesktopRunSessionBaseLocation
defaultDesktopRunSessionBaseLocation()
{
    DesktopRunSessionBaseLocation location;
    const QString appLocalData =
        QStandardPaths::writableLocation(
            QStandardPaths::
                AppLocalDataLocation);
    if (!appLocalData.isEmpty())
    {
        location.parentDirectory =
            normalizedAbsolutePath(
                appLocalData);
    }
    location.baseDirectoryName =
        QString::fromLatin1(
            BaseDirectoryName);
    return location;
}

DesktopRunSessionBaseResult
ensureDefaultDesktopRunSessionBase()
{
    const DesktopRunSessionBaseLocation
        location =
            defaultDesktopRunSessionBaseLocation();
    if (location.parentDirectory.isEmpty())
    {
        DesktopRunSessionBaseResult result;
        fail(
            result,
            DesktopRunSessionBaseError::
                DefaultLocationUnavailable,
            {},
            QStringLiteral(
                "QStandardPaths::AppLocalDataLocation is unavailable"));
        return result;
    }
    if (!QDir().mkpath(
            location.parentDirectory))
    {
        DesktopRunSessionBaseResult result;
        result.path =
            normalizedAbsolutePath(
                location.baseDirectoryPath());
        fail(
            result,
            DesktopRunSessionBaseError::
                DefaultParentCreationFailed,
            location.parentDirectory,
            QStringLiteral(
                "Cannot create the application-local data directory for persistent desktop-run sessions"));
        return result;
    }
    return ensureDesktopRunSessionBase(
        location.parentDirectory,
        location.baseDirectoryName);
}

DesktopRunSessionBaseResult
ensureDesktopRunSessionBase(
    const QString& parentDirectory,
    const QString& baseDirectoryName)
{
    DesktopRunSessionBaseResult result;
    QByteArray parentUtf8;
    if (parentDirectory.isEmpty() ||
        !QDir::isAbsolutePath(
            parentDirectory) ||
        !exactUtf8(
            parentDirectory,
            parentUtf8))
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                InvalidParentPath,
            parentDirectory,
            QStringLiteral(
                "Sessions-base parent must be an absolute path with exact Unicode text"));
        return result;
    }
    if (!validBaseDirectoryName(
            baseDirectoryName))
    {
        fail(
            result,
            DesktopRunSessionBaseError::
                InvalidBaseDirectoryName,
            baseDirectoryName,
            QStringLiteral(
                "Sessions-base name must be one exact direct-child path component"));
        return result;
    }

    const QString parentPath =
        normalizedAbsolutePath(
            parentDirectory);
    const QString basePath =
        QDir(parentPath)
            .filePath(baseDirectoryName);
    result.path = basePath;

#ifdef Q_OS_WIN
    return ensureDesktopRunSessionBaseWindows(
        parentPath,
        baseDirectoryName,
        basePath);
#else
    return ensureDesktopRunSessionBasePosix(
        parentPath,
        baseDirectoryName,
        basePath);
#endif
}
