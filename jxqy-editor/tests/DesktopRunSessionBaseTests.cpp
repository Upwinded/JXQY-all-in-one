#include "../core/DesktopRunSessionBase.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <aclapi.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <iostream>
#include <vector>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

QString normalizedAbsolutePath(
    const QString& path)
{
    return QDir::cleanPath(
        QFileInfo(path).absoluteFilePath());
}

bool writeFile(
    const QString& path,
    const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool pathInside(
    const QString& parent,
    const QString& candidate)
{
    QString normalizedParent =
        QDir::fromNativeSeparators(
            normalizedAbsolutePath(parent));
    const QString normalizedCandidate =
        QDir::fromNativeSeparators(
            normalizedAbsolutePath(candidate));
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity
        PathCaseSensitivity =
            Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity
        PathCaseSensitivity =
            Qt::CaseSensitive;
#endif
    if (normalizedParent.compare(
            normalizedCandidate,
            PathCaseSensitivity) == 0)
    {
        return true;
    }
    if (!normalizedParent.endsWith(
            QLatin1Char('/')))
    {
        normalizedParent.append(
            QLatin1Char('/'));
    }
    return normalizedCandidate.startsWith(
        normalizedParent,
        PathCaseSensitivity);
}

#ifdef Q_OS_WIN
bool currentUserSid(
    HANDLE& token,
    std::vector<unsigned char>& storage,
    PSID& sid)
{
    token = nullptr;
    sid = nullptr;
    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &token))
    {
        return false;
    }
    DWORD bytes = 0;
    GetTokenInformation(
        token,
        TokenUser,
        nullptr,
        0,
        &bytes);
    storage.resize(bytes);
    if (bytes == 0 ||
        !GetTokenInformation(
            token,
            TokenUser,
            storage.data(),
            bytes,
            &bytes))
    {
        CloseHandle(token);
        token = nullptr;
        return false;
    }
    sid =
        reinterpret_cast<TOKEN_USER*>(
            storage.data())->User.Sid;
    return sid != nullptr;
}

bool createdWindowsAclIsPrivate(
    const QString& path)
{
    HANDLE token = nullptr;
    std::vector<unsigned char> tokenStorage;
    PSID currentUser = nullptr;
    if (!currentUserSid(
            token,
            tokenStorage,
            currentUser))
    {
        return false;
    }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PSID owner = nullptr;
    PACL dacl = nullptr;
    const std::wstring nativePath =
        QDir::toNativeSeparators(path)
            .toStdWString();
    const DWORD status =
        GetNamedSecurityInfoW(
            const_cast<LPWSTR>(
                nativePath.c_str()),
            SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION |
                DACL_SECURITY_INFORMATION,
            &owner,
            nullptr,
            &dacl,
            nullptr,
            &descriptor);
    bool privateAcl =
        status == ERROR_SUCCESS &&
        owner != nullptr &&
        EqualSid(owner, currentUser) &&
        dacl != nullptr;
    if (privateAcl)
    {
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        privateAcl =
            GetSecurityDescriptorControl(
                descriptor,
                &control,
                &revision) &&
            (control &
             SE_DACL_PROTECTED) != 0;
    }

    if (descriptor != nullptr)
        LocalFree(descriptor);
    CloseHandle(token);
    return privateAcl;
}

bool grantEveryoneSensitiveAccess(
    const QString& path)
{
    DWORD sidBytes =
        SECURITY_MAX_SID_SIZE;
    std::vector<unsigned char> sidStorage(
        sidBytes);
    if (!CreateWellKnownSid(
            WinWorldSid,
            nullptr,
            sidStorage.data(),
            &sidBytes))
    {
        return false;
    }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL existingDacl = nullptr;
    const std::wstring nativePath =
        QDir::toNativeSeparators(path)
            .toStdWString();
    DWORD status = GetNamedSecurityInfoW(
        const_cast<LPWSTR>(
            nativePath.c_str()),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        &existingDacl,
        nullptr,
        &descriptor);
    PACL replacementDacl = nullptr;
    if (status == ERROR_SUCCESS)
    {
        EXPLICIT_ACCESSW access = {};
        access.grfAccessPermissions =
            FILE_GENERIC_READ |
            FILE_GENERIC_WRITE |
            FILE_DELETE_CHILD;
        access.grfAccessMode = GRANT_ACCESS;
        access.grfInheritance =
            SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        access.Trustee.TrusteeForm =
            TRUSTEE_IS_SID;
        access.Trustee.TrusteeType =
            TRUSTEE_IS_WELL_KNOWN_GROUP;
        access.Trustee.ptstrName =
            static_cast<LPWSTR>(
                reinterpret_cast<PSID>(
                    sidStorage.data()));
        status = SetEntriesInAclW(
            1,
            &access,
            existingDacl,
            &replacementDacl);
    }
    if (status == ERROR_SUCCESS)
    {
        status = SetNamedSecurityInfoW(
            const_cast<LPWSTR>(
                nativePath.c_str()),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION |
                PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            replacementDacl,
            nullptr);
    }
    if (replacementDacl != nullptr)
        LocalFree(replacementDacl);
    if (descriptor != nullptr)
        LocalFree(descriptor);
    return status == ERROR_SUCCESS;
}

bool createDirectorySymbolicLink(
    const QString& target,
    const QString& link)
{
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
    const std::wstring nativeTarget =
        QDir::toNativeSeparators(target)
            .toStdWString();
    const std::wstring nativeLink =
        QDir::toNativeSeparators(link)
            .toStdWString();
    if (CreateSymbolicLinkW(
            nativeLink.c_str(),
            nativeTarget.c_str(),
            SYMBOLIC_LINK_FLAG_DIRECTORY |
                SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
    {
        return true;
    }
    return CreateSymbolicLinkW(
               nativeLink.c_str(),
               nativeTarget.c_str(),
               SYMBOLIC_LINK_FLAG_DIRECTORY) !=
        FALSE;
}
#endif

bool runCreationAndIdempotencyTests()
{
    QTemporaryDir temporaryDirectory(
        QDir(QDir::tempPath()).filePath(
            QString::fromUtf8(
                "desktop-session-base-中文 空格-XXXXXX")));
    if (!check(
            temporaryDirectory.isValid(),
            "create CJK and space temporary parent"))
    {
        return false;
    }

    const QString baseName =
        QString::fromUtf8(
            "会话 基础目录");
    const QString expectedPath =
        QDir(temporaryDirectory.path())
            .filePath(baseName);
    const DesktopRunSessionBaseResult first =
        ensureDesktopRunSessionBase(
            temporaryDirectory.path(),
            baseName);

    bool ok = check(
        first.succeeded() &&
            first.created &&
            QDir::isAbsolutePath(first.path) &&
            first.path ==
                QDir::cleanPath(
                    QFileInfo(expectedPath)
                        .canonicalFilePath()) &&
            QFileInfo(first.path).isDir() &&
            first.problemPath.isEmpty() &&
            first.message.isEmpty(),
        "create a canonical private CJK and space sessions base");
    if (!first.succeeded())
    {
        std::cerr
            << "  error="
            << static_cast<int>(first.error)
            << " problemPath="
            << first.problemPath.toStdString()
            << " message="
            << first.message.toStdString()
            << '\n';
        return false;
    }

#ifdef Q_OS_WIN
    ok = check(
        createdWindowsAclIsPrivate(
            first.path),
        "new Windows base has current-user owner and protected explicit DACL") &&
        ok;
#else
    struct stat information = {};
    const QByteArray nativePath =
        QFile::encodeName(first.path);
    ok = check(
        ::lstat(
            nativePath.constData(),
            &information) == 0 &&
            information.st_uid ==
                ::geteuid() &&
            (information.st_mode &
             07777) == 0700,
        "new POSIX base has current-euid owner and exact mode 0700") &&
        ok;
#endif

    const DesktopRunSessionBaseResult second =
        ensureDesktopRunSessionBase(
            temporaryDirectory.path(),
            baseName);
    ok = check(
        second.succeeded() &&
            !second.created &&
            second.path == first.path &&
            second.problemPath.isEmpty() &&
            second.message.isEmpty(),
        "existing trusted sessions base is idempotently accepted") &&
        ok;

    const DesktopRunSessionBaseResult traversal =
        ensureDesktopRunSessionBase(
            temporaryDirectory.path(),
            QStringLiteral("../outside"));
    ok = check(
        traversal.error ==
                DesktopRunSessionBaseError::
                    InvalidBaseDirectoryName &&
            traversal.path.isEmpty() &&
            traversal.problemPath ==
                QStringLiteral("../outside") &&
            !traversal.message.isEmpty() &&
            !QFileInfo::exists(
                QDir(temporaryDirectory.path())
                    .filePath(
                        QStringLiteral(
                            "../outside"))),
        "explicit base name rejects traversal before filesystem access") &&
        ok;
    return ok;
}

bool runPlaceholderAndPermissionTests()
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create failure-test temporary parent"))
    {
        return false;
    }

    const QString placeholderName =
        QStringLiteral("file-placeholder");
    const QString placeholderPath =
        QDir(temporaryDirectory.path())
            .filePath(placeholderName);
    const QByteArray placeholderBytes(
        "DO-NOT-REPLACE");
    bool ok = check(
        writeFile(
            placeholderPath,
            placeholderBytes),
        "create base file placeholder");
    const DesktopRunSessionBaseResult placeholder =
        ensureDesktopRunSessionBase(
            temporaryDirectory.path(),
            placeholderName);
    ok = check(
        placeholder.error ==
                DesktopRunSessionBaseError::
                    BaseIsNotDirectory &&
            placeholder.problemPath ==
                normalizedAbsolutePath(
                    placeholderPath) &&
            !placeholder.message.isEmpty() &&
            readFile(placeholderPath) ==
                placeholderBytes,
        "file placeholder is rejected without replacement or deletion") &&
        ok;

    const QString unsafeName =
        QStringLiteral("unsafe-permissions");
    const DesktopRunSessionBaseResult
        initiallyPrivate =
            ensureDesktopRunSessionBase(
                temporaryDirectory.path(),
                unsafeName);
    ok = check(
        initiallyPrivate.succeeded(),
        "create directory before unsafe-permission mutation") &&
        ok;
    if (!initiallyPrivate.succeeded())
        return false;

#ifdef Q_OS_WIN
    const bool madeUnsafe =
        grantEveryoneSensitiveAccess(
            initiallyPrivate.path);
#else
    const QByteArray unsafeNative =
        QFile::encodeName(
            initiallyPrivate.path);
    const bool madeUnsafe =
        ::chmod(
            unsafeNative.constData(),
            0755) == 0;
#endif
    ok = check(
        madeUnsafe,
        "make existing sessions base permissions unsafe") &&
        ok;
    if (madeUnsafe)
    {
        const DesktopRunSessionBaseResult unsafe =
            ensureDesktopRunSessionBase(
                temporaryDirectory.path(),
                unsafeName);
        ok = check(
            unsafe.error ==
                    DesktopRunSessionBaseError::
                        BasePermissionsUntrusted &&
                unsafe.problemPath ==
                    initiallyPrivate.path &&
                !unsafe.message.isEmpty() &&
                QFileInfo(
                    initiallyPrivate.path)
                    .isDir(),
            "unsafe existing base is rejected and retained instead of repaired or deleted") &&
            ok;
    }
    return ok;
}

bool runLinkTests()
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create link-test temporary parent"))
    {
        return false;
    }

    const QString target =
        QDir(temporaryDirectory.path())
            .filePath(
                QStringLiteral("target"));
    const QString linkName =
        QStringLiteral("linked-base");
    const QString link =
        QDir(temporaryDirectory.path())
            .filePath(linkName);
    if (!check(
            QDir().mkpath(target),
            "create link target"))
    {
        return false;
    }

#ifdef Q_OS_WIN
    const bool linkCreated =
        createDirectorySymbolicLink(
            target, link);
#else
    const QByteArray targetNative =
        QFile::encodeName(target);
    const QByteArray linkNative =
        QFile::encodeName(link);
    const bool linkCreated =
        ::symlink(
            targetNative.constData(),
            linkNative.constData()) == 0;
#endif
    if (!linkCreated)
    {
        std::cout
            << "SKIP: directory-link creation is unavailable; "
               "base-link rejection runs on capable hosts\n";
        return true;
    }

    const DesktopRunSessionBaseResult linked =
        ensureDesktopRunSessionBase(
            temporaryDirectory.path(),
            linkName);
    return check(
        linked.error ==
                DesktopRunSessionBaseError::
                    BaseIsLink &&
            linked.problemPath ==
                normalizedAbsolutePath(link) &&
            !linked.message.isEmpty() &&
            QFileInfo(target).isDir(),
        "linked sessions base is rejected without touching its target");
}

bool runDefaultLocationTests()
{
    const DesktopRunSessionBaseLocation location =
        defaultDesktopRunSessionBaseLocation();
    const QString path =
        location.baseDirectoryPath();
    const QString appLocalData =
        QStandardPaths::writableLocation(
            QStandardPaths::
                AppLocalDataLocation);
    const QString executableDirectory =
        QCoreApplication::
            applicationDirPath();
    const QString sourceTestDirectory =
        QFileInfo(
            QString::fromUtf8(__FILE__))
            .absolutePath();
    const QString projectDirectory =
        QDir(sourceTestDirectory)
            .absoluteFilePath(
                QStringLiteral("../.."));
    const QString assetsDirectory =
        QDir(projectDirectory)
            .filePath(
                QStringLiteral("assets"));

    return check(
        !location.parentDirectory.isEmpty() &&
            location.parentDirectory ==
                normalizedAbsolutePath(
                    appLocalData) &&
            location.baseDirectoryName ==
                QStringLiteral(
                    "desktop-run-sessions") &&
            QDir::isAbsolutePath(path) &&
            QFileInfo(path)
                    .absolutePath() ==
                location.parentDirectory &&
            !pathInside(
                executableDirectory,
                path) &&
            !pathInside(
                assetsDirectory,
                path) &&
            !pathInside(
                projectDirectory,
                path),
        "default location is a fixed AppLocalData direct child outside executable, project, and assets directories");
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(
        QStringLiteral("JxqyEditorTests"));
    QCoreApplication::setApplicationName(
        QStringLiteral(
            "DesktopRunSessionBaseTests"));

    const bool ok =
        runCreationAndIdempotencyTests() &&
        runPlaceholderAndPermissionTests() &&
        runLinkTests() &&
        runDefaultLocationTests();
    if (!ok)
        return 1;
    std::cout
        << "Desktop run session base tests passed\n";
    return 0;
}
