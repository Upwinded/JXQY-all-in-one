#include "../core/DesktopRunSessionWorkspace.h"
#include "../core/SavedSceneLaunchPreparation.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <aclapi.h>
#include <windows.h>
#include <winioctl.h>
#endif

namespace
{
namespace fs = std::filesystem;

bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

bool writeFile(
    const QString& path,
    const QByteArray& bytes)
{
    if (!QDir().mkpath(
            QFileInfo(path).absolutePath()))
    {
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) &&
        file.write(bytes) == bytes.size() &&
        file.commit();
}

bool overwriteFile(
    const QString& path,
    const QByteArray& bytes)
{
    QFile file(path);
    return file.open(
               QIODevice::WriteOnly |
               QIODevice::Truncate) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return file.readAll();
}

fs::path hostPath(const QString& path)
{
#ifdef Q_OS_WIN
    return fs::path(
        reinterpret_cast<const wchar_t*>(
            path.utf16()));
#else
    const QByteArray utf8 = path.toUtf8();
    return fs::u8path(
        utf8.constData(),
        utf8.constData() + utf8.size());
#endif
}

EditorRun::Descriptor validDescriptor(
    const QString& resourceRoot)
{
    EditorRun::Descriptor descriptor;
    descriptor.assetsCollectionRoot =
        hostPath(resourceRoot);
    descriptor.activeResourcePackId = "JXQY2";
    descriptor.target.sceneId = "scene-cjk";
    descriptor.target.sceneName = u8"中都 测试";
    descriptor.target.mapPath = u8"map/中都.map";
    descriptor.target.npcPath =
        u8"ini/npc/中都.npc";
    descriptor.target.objectPath =
        u8"ini/obj/中都.obj";
    descriptor.target.entryScriptPath =
        u8"script/中都/入口.txt";
    descriptor.target.playerX = 100;
    descriptor.target.playerY = 120;
    descriptor.target.integerVariables.emplace(
        "Event", 100);
    return descriptor;
}

DesktopRunSessionFormalRoots resourceRoots(
    const QString& activeRoot,
    const QStringList& dependencies = {})
{
    DesktopRunSessionFormalRoots roots;
    roots.resourceRoots = dependencies;
    roots.resourceRoots.append(activeRoot);
    roots.saveRoot = activeRoot;
    return roots;
}

bool createResourceFixture(
    const QString& resourceRoot)
{
    return writeFile(
               QDir(resourceRoot).filePath(
                   QString::fromUtf8(
                       "map/中都.map")),
               QByteArray("MAP0")) &&
        writeFile(
            QDir(resourceRoot).filePath(
                QString::fromUtf8(
                    "script/中都/入口.txt")),
            QByteArray("SCRIPT0")) &&
        writeFile(
            QDir(resourceRoot).filePath(
                QString::fromUtf8(
                    "ini/npc/中都.npc")),
            QByteArray("NPC0"));
}

#ifdef Q_OS_WIN
bool setPrivateDirectoryAcl(
    const QString& path)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &token))
    {
        return false;
    }
    DWORD tokenBytes = 0;
    GetTokenInformation(
        token,
        TokenUser,
        nullptr,
        0,
        &tokenBytes);
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
        return false;
    }
    const PSID currentUser =
        reinterpret_cast<TOKEN_USER*>(
            tokenStorage.data())->User.Sid;

    EXPLICIT_ACCESSW access = {};
    access.grfAccessPermissions =
        FILE_ALL_ACCESS;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance =
        SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm =
        TRUSTEE_IS_SID;
    access.Trustee.TrusteeType =
        TRUSTEE_IS_USER;
    access.Trustee.ptstrName =
        static_cast<LPWSTR>(currentUser);
    PACL dacl = nullptr;
    DWORD status = SetEntriesInAclW(
        1,
        &access,
        nullptr,
        &dacl);
    if (status == ERROR_SUCCESS)
    {
        std::wstring nativePath =
            QDir::toNativeSeparators(path)
                .toStdWString();
        status = SetNamedSecurityInfoW(
            nativePath.data(),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION |
                PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            dacl,
            nullptr);
    }
    if (dacl != nullptr)
    {
        LocalFree(dacl);
    }
    CloseHandle(token);
    return status == ERROR_SUCCESS;
}

bool createDirectoryJunction(
    const QString& targetPath,
    const QString& junctionPath)
{
    if (!QDir().mkdir(junctionPath))
    {
        return false;
    }

    const QString nativeTarget =
        QDir::toNativeSeparators(
            QFileInfo(targetPath).
                absoluteFilePath());
    const std::wstring substituteName =
        std::wstring(L"\\??\\") +
        nativeTarget.toStdWString();
    const std::wstring printName =
        nativeTarget.toStdWString();
    const qsizetype substituteBytes =
        static_cast<qsizetype>(
            substituteName.size() *
            sizeof(wchar_t));
    const qsizetype printBytes =
        static_cast<qsizetype>(
            printName.size() *
            sizeof(wchar_t));

    struct MountPointReparseBuffer
    {
        DWORD reparseTag;
        WORD reparseDataLength;
        WORD reserved;
        WORD substituteNameOffset;
        WORD substituteNameLength;
        WORD printNameOffset;
        WORD printNameLength;
        wchar_t pathBuffer[
            MAXIMUM_REPARSE_DATA_BUFFER_SIZE /
            sizeof(wchar_t)];
    };

    MountPointReparseBuffer buffer = {};
    buffer.reparseTag =
        IO_REPARSE_TAG_MOUNT_POINT;
    buffer.substituteNameLength =
        static_cast<WORD>(substituteBytes);
    buffer.printNameOffset =
        static_cast<WORD>(
            substituteBytes +
            sizeof(wchar_t));
    buffer.printNameLength =
        static_cast<WORD>(printBytes);
    buffer.reparseDataLength =
        static_cast<WORD>(
            4 * sizeof(WORD) +
            substituteBytes +
            sizeof(wchar_t) +
            printBytes +
            sizeof(wchar_t));
    if (8 + buffer.reparseDataLength >
        MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        QDir().rmdir(junctionPath);
        return false;
    }
    std::copy(
        substituteName.begin(),
        substituteName.end(),
        buffer.pathBuffer);
    std::copy(
        printName.begin(),
        printName.end(),
        reinterpret_cast<wchar_t*>(
            reinterpret_cast<char*>(
                buffer.pathBuffer) +
            buffer.printNameOffset));

    const std::wstring nativeJunction =
        QDir::toNativeSeparators(
            junctionPath).toStdWString();
    HANDLE handle = CreateFileW(
        nativeJunction.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        QDir().rmdir(junctionPath);
        return false;
    }
    DWORD returnedBytes = 0;
    const BOOL succeeded = DeviceIoControl(
        handle,
        FSCTL_SET_REPARSE_POINT,
        &buffer,
        8 + buffer.reparseDataLength,
        nullptr,
        0,
        &returnedBytes,
        nullptr);
    CloseHandle(handle);
    if (!succeeded)
    {
        QDir().rmdir(junctionPath);
    }
    return succeeded != FALSE;
}
#endif

bool createPrivateDirectory(
    const QString& path)
{
    if (!QDir().mkpath(path))
    {
        return false;
    }
#ifdef Q_OS_WIN
    return setPrivateDirectoryAcl(path);
#else
    return QFile::setPermissions(
        path,
        QFileDevice::ReadOwner |
            QFileDevice::WriteOwner |
            QFileDevice::ExeOwner);
#endif
}

bool createDirectoryRedirect(
    const QString& targetPath,
    const QString& redirectPath)
{
    std::error_code error;
    fs::create_directory_symlink(
        hostPath(targetPath),
        hostPath(redirectPath),
        error);
    if (!error)
    {
        return true;
    }
#ifdef Q_OS_WIN
    return createDirectoryJunction(
        targetPath,
        redirectPath);
#else
    return false;
#endif
}

bool createHardLink(
    const QString& existingPath,
    const QString& linkPath)
{
    std::error_code error;
    fs::create_hard_link(
        hostPath(existingPath),
        hostPath(linkPath),
        error);
    return !error;
}

PreparedDesktopRunOverlayFile overlayFile(
    const QString& resourceRoot,
    const QString& virtualPath,
    const QByteArray& bytes,
    ProjectDocumentType documentType)
{
    PreparedDesktopRunOverlayFile file;
    file.virtualPath = virtualPath;
    file.sourcePath =
        QDir(resourceRoot).filePath(
            virtualPath);
    file.documentType = documentType;
    file.bytes = bytes;
    file.sha256 =
        QCryptographicHash::hash(
            bytes,
            QCryptographicHash::Sha256);
    return file;
}

PreparedSavedSceneLaunch preparedLaunch(
    const QString& resourceRoot)
{
    PreparedSavedSceneLaunch launch;
    launch.assetsCollectionRoot = resourceRoot;
    launch.canonicalActiveResourcePackId =
        QStringLiteral("JXQY2");
    launch.activeContentRoot = resourceRoot;

    ResourceContentRoot contentRoot;
    contentRoot.rootPath = resourceRoot;
    contentRoot.id = QStringLiteral("JXQY2");
    contentRoot.name = QStringLiteral("JXQY2");
    contentRoot.kind =
        ResourceContentRoot::Kind::Local;
    contentRoot.available = true;
    launch.orderedContentRoots.append(contentRoot);
    launch.formalRoots = {resourceRoot};

    launch.targetKind =
        EditorRun::TargetKind::Scene;
    launch.scene.id =
        QStringLiteral("scene-cjk");
    launch.scene.name =
        QString::fromUtf8("中都 测试");
    launch.scene.mapPath =
        QString::fromUtf8("map/中都.map");
    launch.scene.npcPath =
        QString::fromUtf8(
            "ini/npc/中都.npc");
    launch.scene.objectPath =
        QString::fromUtf8(
            "ini/obj/中都.obj");
    launch.scene.entryScriptPath =
        QString::fromUtf8(
            "script/中都/入口.txt");
    launch.scene.playerPosition =
        QPoint(100, 120);
    launch.scene.integerVariables.insert(
        QStringLiteral("Event"),
        100);
    return launch;
}

QStringList sortedKeys(
    const QJsonObject& object)
{
    QStringList keys = object.keys();
    keys.sort();
    return keys;
}

bool validateWorkspaceTopology(
    const DesktopRunSessionWorkspaceResult& result)
{
    bool ok = check(
        result.succeeded(),
        "workspace creation succeeds");
    if (!result.succeeded())
    {
        std::cerr
            << "  error="
            << static_cast<int>(result.error)
            << " path="
            << result.problemPath.toStdString()
            << " message="
            << result.message.toStdString()
            << '\n';
        return false;
    }

    const QRegularExpression canonicalUuid(
        QStringLiteral(
            "^[0-9a-f]{8}-[0-9a-f]{4}-"
            "[0-9a-f]{4}-[0-9a-f]{4}-"
            "[0-9a-f]{12}$"));
    ok = check(
        canonicalUuid.match(
            result.workspace->sessionId)
            .hasMatch() &&
            result.sessionId ==
                result.workspace->sessionId,
        "session uses one canonical UUID") && ok;

    const QStringList leaves = {
        result.paths.overlayRoot,
        result.paths.isolatedSaveRoot,
        result.paths.applicationStateRoot,
        result.paths.diagnosticsRoot
    };
    bool distinctLeaves = true;
    for (qsizetype left = 0;
         left < leaves.size();
         ++left)
    {
        if (!QFileInfo(leaves.at(left)).isDir())
        {
            distinctLeaves = false;
        }
        for (qsizetype right = left + 1;
             right < leaves.size();
             ++right)
        {
            std::error_code error;
            if (fs::equivalent(
                    hostPath(leaves.at(left)),
                    hostPath(leaves.at(right)),
                    error) ||
                error)
            {
                distinctLeaves = false;
            }
        }
    }
    ok = check(
        QFileInfo(result.paths.sessionRoot).isDir() &&
            distinctLeaves,
        "session owns four physically distinct output leaves") &&
        ok;
    ok = check(
        QFileInfo(result.paths.markerPath).isFile() &&
            QFileInfo(
                result.paths.
                    resourceRoutingContractPath).
                isFile() &&
            QFileInfo(
                result.paths.descriptorPath).isFile(),
        "workspace publishes marker, routing contract, and descriptor") &&
        ok;

    const QByteArray descriptorBytes =
        readFile(result.paths.descriptorPath);
    const EditorRun::DescriptorResult parsed =
        EditorRun::parseEditorRunDescriptor(
            std::string_view(
                descriptorBytes.constData(),
                static_cast<std::size_t>(
                    descriptorBytes.size())));
    ok = check(
        parsed.succeeded() &&
            parsed.descriptor.sessionId ==
                result.sessionId.toStdString() &&
            parsed.descriptor.overlayRoot ==
                hostPath(result.paths.overlayRoot) &&
            parsed.descriptor.isolatedSaveRoot ==
                hostPath(
                    result.paths.
                        isolatedSaveRoot) &&
            parsed.descriptor.applicationStateRoot ==
                hostPath(
                    result.paths.
                        applicationStateRoot) &&
            parsed.descriptor.diagnosticsPath ==
                hostPath(
                    result.paths.diagnosticsPath) &&
            parsed.descriptor.logPath ==
                hostPath(result.paths.logPath) &&
            result.workspace->descriptorSha256 ==
                QCryptographicHash::hash(
                    descriptorBytes,
                    QCryptographicHash::Sha256).
                    toHex(),
        "descriptor binds the game only to private session outputs") &&
        ok;
    return ok;
}

bool validateRoutingContract(
    const DesktopRunSessionWorkspaceResult& result,
    bool expectOverlayOrigins)
{
    QJsonParseError parseError;
    const QByteArray bytes =
        readFile(
            result.paths.
                resourceRoutingContractPath);
    const QJsonDocument document =
        QJsonDocument::fromJson(
            bytes,
            &parseError);
    if (!check(
            parseError.error ==
                    QJsonParseError::NoError &&
                document.isObject(),
            "resource routing contract is valid JSON"))
    {
        return false;
    }

    const QJsonObject contract =
        document.object();
    QStringList expectedTopLevelKeys = {
        QStringLiteral("roots"),
        QStringLiteral("schemaVersion")
    };
    if (expectOverlayOrigins)
    {
        expectedTopLevelKeys.append(
            QStringLiteral(
                "traceOverlayOrigins"));
    }
    expectedTopLevelKeys.sort();
    bool ok = check(
        sortedKeys(contract) ==
                expectedTopLevelKeys &&
            contract.value(
                QStringLiteral(
                    "schemaVersion")).toInt() == 1,
        "routing contract contains only version, roots, and optional overlay origins");

    const QJsonArray roots =
        contract.value(
            QStringLiteral("roots")).toArray();
    ok = check(
        !roots.isEmpty(),
        "routing contract retains at least one resource route") &&
        ok;
    for (const QJsonValue& rootValue : roots)
    {
        const QJsonObject root =
            rootValue.toObject();
        ok = check(
            sortedKeys(root) ==
                QStringList({
                    QStringLiteral("path"),
                    QStringLiteral("roles")
                }) &&
                QDir::isAbsolutePath(
                    root.value(
                        QStringLiteral("path")).
                        toString()) &&
                !root.value(
                    QStringLiteral("roles")).
                    toArray().isEmpty(),
            "each route contains only an absolute path and roles") &&
            ok;
    }
    if (expectOverlayOrigins)
    {
        const QJsonArray origins =
            contract.value(
                QStringLiteral(
                    "traceOverlayOrigins")).
                toArray();
        ok = check(
            !origins.isEmpty(),
            "prepared overlay retains portable source-route metadata") &&
            ok;
        for (const QJsonValue& originValue :
             origins)
        {
            const QJsonObject origin =
                originValue.toObject();
            ok = check(
                sortedKeys(origin) ==
                        QStringList({
                            QStringLiteral(
                                "resourcePackId"),
                            QStringLiteral(
                                "rootKind"),
                            QStringLiteral(
                                "rootOrdinal"),
                            QStringLiteral(
                                "rootPath"),
                            QStringLiteral(
                                "virtualPath")
                        }) &&
                    origin.value(
                        QStringLiteral(
                            "rootOrdinal")).
                        isDouble() &&
                    !origin.value(
                        QStringLiteral(
                            "virtualPath")).
                        toString().isEmpty() &&
                    origin.value(
                        QStringLiteral(
                            "rootKind")).
                        isString() &&
                    origin.value(
                        QStringLiteral(
                            "resourcePackId")).
                        isString() &&
                    QDir::isAbsolutePath(
                        origin.value(
                            QStringLiteral(
                                "rootPath")).
                            toString()) &&
                    QDir::cleanPath(
                        origin.value(
                            QStringLiteral(
                                "rootPath")).
                            toString()) ==
                        origin.value(
                            QStringLiteral(
                                "rootPath")).
                            toString(),
                "each prepared overlay origin contains stable logical source-root provenance") &&
                ok;
        }
    }
    const QList<QByteArray> forbiddenNames = {
        QByteArray("\"entries\""),
        QByteArray("\"hashAlgorithm\""),
        QByteArray("\"sha256\""),
        QByteArray("\"mtime\""),
        QByteArray("\"device\""),
        QByteArray("\"nodeHigh\""),
        QByteArray("\"nodeLow\""),
        QByteArray("\"linkCount\"")
    };
    for (const QByteArray& name : forbiddenNames)
    {
        ok = check(
            !bytes.contains(name),
            "routing contract contains no resource content or root identity metadata") &&
            ok;
    }
    ok = check(
        result.workspace->
                resourceRoutingContractSha256 ==
            QCryptographicHash::hash(
                bytes,
                QCryptographicHash::Sha256).
                toHex(),
        "workspace retains the exact lightweight routing-contract digest") &&
        ok;
    return ok;
}

QStringList routingRolesForPath(
    const DesktopRunSessionWorkspaceResult& result,
    const QString& path)
{
    const QJsonObject contract =
        QJsonDocument::fromJson(
            readFile(
                result.paths.
                    resourceRoutingContractPath)).
            object();
    const QString expected =
        QDir::fromNativeSeparators(
            QFileInfo(path).
                absoluteFilePath());
    for (const QJsonValue& rootValue :
         contract.value(
             QStringLiteral("roots")).toArray())
    {
        const QJsonObject root =
            rootValue.toObject();
        const QString actual =
            QDir::fromNativeSeparators(
                root.value(
                    QStringLiteral("path")).
                    toString());
#ifdef Q_OS_WIN
        constexpr Qt::CaseSensitivity
            PathCaseSensitivity =
                Qt::CaseInsensitive;
#else
        constexpr Qt::CaseSensitivity
            PathCaseSensitivity =
                Qt::CaseSensitive;
#endif
        if (actual.compare(
                expected,
                PathCaseSensitivity) != 0)
        {
            continue;
        }
        QStringList roles;
        for (const QJsonValue& role :
             root.value(
                 QStringLiteral("roles")).
                 toArray())
        {
            roles.append(role.toString());
        }
        return roles;
    }
    return {};
}

bool runOpenResourceContractTests()
{
    QTemporaryDir temporaryDirectory(
        QDir::tempPath() +
        QString::fromUtf8(
            "/jxqy 开放资源会话-XXXXXX"));
    if (!check(
            temporaryDirectory.isValid(),
            "create open-resource temporary root"))
    {
        return false;
    }

    const QString sessionsBase =
        QDir(temporaryDirectory.path()).filePath(
            QString::fromUtf8("私有会话"));
    const QString activeRoot =
        QDir(temporaryDirectory.path()).filePath(
            QString::fromUtf8("活动资源"));
    const QString dependencyRoot =
        QDir(temporaryDirectory.path()).filePath(
            QString::fromUtf8("依赖资源"));
    bool ok = check(
        createPrivateDirectory(sessionsBase) &&
            QDir().mkpath(activeRoot) &&
            createResourceFixture(activeRoot) &&
            QDir().mkpath(dependencyRoot) &&
            writeFile(
                QDir(dependencyRoot).filePath(
                    QStringLiteral(
                        "common.ini")),
                QByteArray("COMMON")),
        "create private output and ordinary resource roots");
    if (!ok)
    {
        return false;
    }

    DesktopRunSessionWorkspaceResult workspace =
        createDesktopRunSessionWorkspace(
            sessionsBase,
            validDescriptor(activeRoot),
            resourceRoots(
                activeRoot,
                {dependencyRoot}));
    ok = validateWorkspaceTopology(workspace) &&
        ok;
    if (!workspace.succeeded())
    {
        return false;
    }
    ok = validateRoutingContract(
             workspace,
             false) &&
        ok;
    ok = check(
        routingRolesForPath(
            workspace,
            activeRoot) ==
                QStringList({
                    QStringLiteral("resource"),
                    QStringLiteral("save")
                }) &&
            routingRolesForPath(
                workspace,
                dependencyRoot) ==
                QStringList({
                    QStringLiteral("resource")
                }),
        "routing contract preserves only the resource and save roles assigned to each path") &&
        ok;

    const QString mapPath =
        QDir(activeRoot).filePath(
            QString::fromUtf8(
                "map/中都.map"));
    const QString removedPath =
        QDir(activeRoot).filePath(
            QString::fromUtf8(
                "ini/npc/中都.npc"));
    const QString addedPath =
        QDir(activeRoot).filePath(
            QString::fromUtf8(
                "用户新增/随手改.lua"));
    ok = check(
        overwriteFile(
            mapPath,
            QByteArray("MAP-EDITED-OUTSIDE")) &&
            QFile::remove(removedPath) &&
            writeFile(
                addedPath,
                QByteArray(
                    "return 'external edit'")),
        "external tools may modify, delete, and add resource files while the session exists") &&
        ok;

    const QString movedRoot =
        activeRoot +
        QStringLiteral("-moved");
    std::error_code moveError;
    fs::rename(
        hostPath(activeRoot),
        hostPath(movedRoot),
        moveError);
    ok = check(
        !moveError,
        "resource root can be replaced at the same path after workspace creation") &&
        ok;

    const QString redirectedRoot =
        QDir(temporaryDirectory.path()).filePath(
            QString::fromUtf8(
                "重定向资源"));
    const bool redirectTargetReady =
        QDir().mkpath(redirectedRoot) &&
        writeFile(
            QDir(redirectedRoot).filePath(
                QStringLiteral(
                    "replacement.txt")),
            QByteArray("replacement"));
    const bool redirected =
        redirectTargetReady &&
        createDirectoryRedirect(
            redirectedRoot,
            activeRoot);
    if (!redirected)
    {
        std::cerr
            << "SKIP: directory redirection is unavailable; using a normal same-path replacement\n";
        ok = check(
            redirectTargetReady &&
                QDir().mkpath(activeRoot),
            "fallback same-path resource root replacement succeeds") &&
            ok;
    }

    ok = check(
        readFile(
            workspace.paths.
                resourceRoutingContractPath) !=
                readFile(
                    workspace.paths.descriptorPath),
        "routing contract and launch descriptor remain separate") &&
        ok;

    DesktopRunSessionWorkspaceResult
        replacementWorkspace =
            createDesktopRunSessionWorkspace(
                sessionsBase,
                validDescriptor(activeRoot),
                resourceRoots(activeRoot));
    ok = check(
        replacementWorkspace.succeeded(),
        redirected
        ? "a redirected resource root is accepted for a later open-game run"
        : "a replaced resource root is accepted for a later open-game run") &&
        ok;
    return ok;
}

bool runPreparedOverlayTests()
{
    QTemporaryDir temporaryDirectory(
        QDir::tempPath() +
        QString::fromUtf8(
            "/jxqy 私有覆盖层-XXXXXX"));
    if (!check(
            temporaryDirectory.isValid(),
            "create sparse-overlay temporary root"))
    {
        return false;
    }

    const QString sessionsBase =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("sessions"));
    const QString resourceRoot =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("resources"));
    const QString dependencyIdRoot =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("dependency-id"));
    const QString dependencyPathRoot =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("dependency-path"));
    const QString commonRoot =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("common"));
    bool ok = check(
        createPrivateDirectory(sessionsBase) &&
            QDir().mkpath(resourceRoot) &&
            createResourceFixture(resourceRoot) &&
            QDir().mkpath(dependencyIdRoot) &&
            QDir().mkpath(dependencyPathRoot) &&
            QDir().mkpath(commonRoot),
        "create sparse-overlay fixtures");
    if (!ok)
    {
        return false;
    }

    PreparedSavedSceneLaunch launch =
        preparedLaunch(resourceRoot);
    ResourceContentRoot dependencyId;
    dependencyId.rootPath =
        dependencyIdRoot;
    dependencyId.id =
        QStringLiteral("BASE");
    dependencyId.name =
        QStringLiteral("Base");
    dependencyId.kind =
        ResourceContentRoot::Kind::
            DependencyId;
    dependencyId.available = true;
    launch.orderedContentRoots.append(
        dependencyId);

    ResourceContentRoot dependencyPath;
    dependencyPath.rootPath =
        dependencyPathRoot;
    dependencyPath.kind =
        ResourceContentRoot::Kind::DependencyId;
    dependencyPath.id =
        QStringLiteral("PATHBASE");
    dependencyPath.available = true;
    launch.orderedContentRoots.append(
        dependencyPath);

    ResourceContentRoot common;
    common.rootPath = commonRoot;
    common.kind =
        ResourceContentRoot::Kind::Common;
    common.available = true;
    launch.orderedContentRoots.append(
        common);
    launch.formalRoots.append(
        {dependencyIdRoot,
         dependencyPathRoot,
         commonRoot});
    launch.overlayFiles = {
        overlayFile(
            resourceRoot,
            QString::fromUtf8(
                "script/中都/入口.txt"),
            QByteArray("SCRIPT-OVERLAY"),
            ProjectDocumentType::Script),
        overlayFile(
            dependencyIdRoot,
            QString::fromUtf8(
                "map/中都.map"),
            QByteArray("MAP-OVERLAY"),
            ProjectDocumentType::Map),
        overlayFile(
            dependencyPathRoot,
            QStringLiteral(
                "ini/runtime/test.ini"),
            QByteArray("[test]\nvalue=1\n"),
            ProjectDocumentType::Text),
        overlayFile(
            commonRoot,
            QStringLiteral(
                "ui/common.txt"),
            QByteArray("COMMON-OVERLAY"),
            ProjectDocumentType::Text)
    };
    launch.overlayFiles[1].
        contentRootOrdinal = 1;
    launch.overlayFiles[2].
        contentRootOrdinal = 2;
    launch.overlayFiles[3].
        contentRootOrdinal = 3;

    DesktopRunSessionWorkspaceResult published =
        createDesktopRunSessionWorkspace(
            sessionsBase,
            launch);
    ok = validateWorkspaceTopology(published) &&
        ok;
    if (!published.succeeded())
    {
        return false;
    }
    ok = validateRoutingContract(
             published,
             true) &&
        ok;
    const QJsonArray publishedOrigins =
        QJsonDocument::fromJson(
            readFile(
                published.paths.
                    resourceRoutingContractPath)).
            object().
            value(
                QStringLiteral(
                    "traceOverlayOrigins")).
            toArray();
    const auto originFor =
        [&publishedOrigins](
            const QString& virtualPath)
            -> QJsonObject
        {
            for (const QJsonValue& value :
                 publishedOrigins)
            {
                const QJsonObject origin =
                    value.toObject();
                if (origin.value(
                        QStringLiteral(
                            "virtualPath")).
                        toString() ==
                    virtualPath)
                {
                    return origin;
                }
            }
            return {};
        };
    const auto normalizedRoot =
        [](const QString& path)
        {
            return QDir::fromNativeSeparators(
                QDir::cleanPath(
                    QFileInfo(path).
                        absoluteFilePath()));
        };
    const QJsonObject activeOrigin =
        originFor(
            QString::fromUtf8(
                "script/中都/入口.txt"));
    const QJsonObject dependencyIdOrigin =
        originFor(
            QString::fromUtf8(
                "map/中都.map"));
    const QJsonObject dependencyPathOrigin =
        originFor(
            QStringLiteral(
                "ini/runtime/test.ini"));
    const QJsonObject commonOrigin =
        originFor(
            QStringLiteral(
                "ui/common.txt"));
    ok = check(
        activeOrigin.value(
            QStringLiteral(
                "rootKind")).toString() ==
                QStringLiteral("active") &&
            activeOrigin.value(
                QStringLiteral(
                    "resourcePackId")).
                toString() ==
                QStringLiteral("JXQY2") &&
            activeOrigin.value(
                QStringLiteral(
                    "rootPath")).toString() ==
                normalizedRoot(resourceRoot) &&
            activeOrigin.value(
                QStringLiteral(
                    "rootOrdinal")).toInt() == 0 &&
            dependencyIdOrigin.value(
                QStringLiteral(
                    "rootKind")).toString() ==
                QStringLiteral(
                    "dependency-id") &&
            dependencyIdOrigin.value(
                QStringLiteral(
                    "resourcePackId")).
                toString() ==
                QStringLiteral("BASE") &&
            dependencyIdOrigin.value(
                QStringLiteral(
                    "rootPath")).toString() ==
                normalizedRoot(
                    dependencyIdRoot) &&
            dependencyIdOrigin.value(
                QStringLiteral(
                    "rootOrdinal")).toInt() == 1 &&
            dependencyPathOrigin.value(
                QStringLiteral(
                    "rootKind")).toString() ==
                QStringLiteral(
                    "dependency-id") &&
            dependencyPathOrigin.value(
                QStringLiteral(
                    "resourcePackId")).
                toString() == QStringLiteral("PATHBASE") &&
            dependencyPathOrigin.value(
                QStringLiteral(
                    "rootPath")).toString() ==
                normalizedRoot(
                    dependencyPathRoot) &&
            dependencyPathOrigin.value(
                QStringLiteral(
                    "rootOrdinal")).toInt() == 2 &&
            commonOrigin.value(
                QStringLiteral(
                    "rootKind")).toString() ==
                QStringLiteral("common") &&
            commonOrigin.value(
                QStringLiteral(
                    "resourcePackId")).
                toString().isEmpty() &&
            commonOrigin.value(
                QStringLiteral(
                    "rootPath")).toString() ==
                normalizedRoot(commonRoot) &&
            commonOrigin.value(
                QStringLiteral(
                    "rootOrdinal")).toInt() == 3,
        "prepared overlay origins preserve stable keys for every logical root kind") &&
        ok;
    ok = check(
        readFile(
            QDir(published.paths.overlayRoot).
                filePath(
                    QString::fromUtf8(
                        "script/中都/入口.txt"))) ==
                QByteArray("SCRIPT-OVERLAY") &&
            readFile(
                QDir(published.paths.overlayRoot).
                    filePath(
                        QString::fromUtf8(
                            "map/中都.map"))) ==
                QByteArray("MAP-OVERLAY") &&
            readFile(
                QDir(published.paths.overlayRoot).
                 filePath(
                     QStringLiteral(
                         "ini/runtime/test.ini"))) ==
                 QByteArray("[test]\nvalue=1\n") &&
            readFile(
                QDir(published.paths.overlayRoot).
                    filePath(
                        QStringLiteral(
                            "ui/common.txt"))) ==
                QByteArray("COMMON-OVERLAY") &&
            !QFileInfo::exists(
                QDir(published.paths.sessionRoot).
                    filePath(
                        QStringLiteral(
                            ".overlay-staging"))) &&
            readFile(
                QDir(resourceRoot).filePath(
                    QString::fromUtf8(
                        "map/中都.map"))) ==
                QByteArray("MAP0"),
        "prepared files are atomically published only inside the private overlay") &&
        ok;

    PreparedSavedSceneLaunch traversal =
        launch;
    traversal.overlayFiles = {
        overlayFile(
            resourceRoot,
            QStringLiteral(
                "../outside.txt"),
            QByteArray("escape"),
            ProjectDocumentType::Script)
    };
    const DesktopRunSessionWorkspaceResult
        traversalResult =
            createDesktopRunSessionWorkspace(
                sessionsBase,
                traversal);
    ok = check(
        traversalResult.error ==
                DesktopRunSessionWorkspaceError::
                    PreparedOverlayInvalid &&
            traversalResult.sessionId.isEmpty(),
        "overlay path traversal is rejected before creating a session") &&
        ok;

    PreparedSavedSceneLaunch collision =
        launch;
    collision.overlayFiles = {
        overlayFile(
            resourceRoot,
            QStringLiteral("path"),
            QByteArray("file"),
            ProjectDocumentType::Script),
        overlayFile(
            resourceRoot,
            QStringLiteral("path/child"),
            QByteArray("child"),
            ProjectDocumentType::Script)
    };
    const DesktopRunSessionWorkspaceResult
        collisionResult =
            createDesktopRunSessionWorkspace(
                sessionsBase,
                collision);
    ok = check(
        collisionResult.error ==
                DesktopRunSessionWorkspaceError::
                    PreparedOverlayPathCollision &&
            collisionResult.sessionId.isEmpty(),
        "overlay file-directory prefix collisions are rejected exactly") &&
        ok;

    bool hardLinkAttempted = false;
    bool hardLinkCreated = false;
    setDesktopRunSessionWorkspaceFaultInjectorForTests(
        [&hardLinkAttempted,
         &hardLinkCreated](
            DesktopRunSessionWorkspaceFaultPoint point,
            const QString& path)
        {
            if (hardLinkAttempted ||
                point !=
                    DesktopRunSessionWorkspaceFaultPoint::
                        DuringOverlayFileWrite)
            {
                return false;
            }
            hardLinkAttempted = true;
            hardLinkCreated = createHardLink(
                path,
                path +
                    QStringLiteral(
                        ".injected-hardlink"));
            return false;
        });
    const DesktopRunSessionWorkspaceResult
        hardLinked =
            createDesktopRunSessionWorkspace(
                sessionsBase,
                launch);
    setDesktopRunSessionWorkspaceFaultInjectorForTests(
        {});
    if (hardLinkCreated)
    {
        ok = check(
            hardLinked.error ==
                    DesktopRunSessionWorkspaceError::
                        OverlayMaterializationFailed &&
                !QFileInfo::exists(
                    hardLinked.paths.overlayRoot) &&
                !QFileInfo::exists(
                    hardLinked.paths.descriptorPath),
            "a hard link injected into staging is rejected before overlay publication") &&
            ok;
    }
    else
    {
        std::cerr
            << "SKIP: sparse-overlay hard-link injection is unavailable\n";
        ok = check(
            hardLinkAttempted &&
                hardLinked.succeeded(),
            "unavailable hard-link injection leaves normal overlay publication intact") &&
            ok;
    }

    bool redirectAttempted = false;
    bool redirectCreated = false;
    setDesktopRunSessionWorkspaceFaultInjectorForTests(
        [&redirectAttempted,
         &redirectCreated,
         resourceRoot](
            DesktopRunSessionWorkspaceFaultPoint point,
            const QString& stagingPath)
        {
            if (redirectAttempted ||
                point !=
                    DesktopRunSessionWorkspaceFaultPoint::
                        BeforeOverlayStaging)
            {
                return false;
            }
            redirectAttempted = true;
            redirectCreated =
                createDirectoryRedirect(
                    resourceRoot,
                    QDir(stagingPath).filePath(
                        QStringLiteral("map")));
            return false;
        });
    const DesktopRunSessionWorkspaceResult
        redirected =
            createDesktopRunSessionWorkspace(
                sessionsBase,
                launch);
    setDesktopRunSessionWorkspaceFaultInjectorForTests(
        {});
    if (redirectCreated)
    {
        ok = check(
            redirected.error ==
                    DesktopRunSessionWorkspaceError::
                        OverlayMaterializationFailed &&
                !QFileInfo::exists(
                    redirected.paths.overlayRoot) &&
                !QFileInfo::exists(
                    redirected.paths.descriptorPath) &&
                readFile(
                    QDir(resourceRoot).filePath(
                        QString::fromUtf8(
                            "map/中都.map"))) ==
                    QByteArray("MAP0"),
            "a redirect injected into private staging is rejected without writing its target") &&
            ok;
    }
    else
    {
        std::cerr
            << "SKIP: sparse-overlay redirect injection is unavailable\n";
        ok = check(
            redirectAttempted &&
                redirected.succeeded(),
            "unavailable redirect injection leaves normal overlay publication intact") &&
            ok;
    }
    return ok;
}

bool runPrivateOutputSafetyTests()
{
    QTemporaryDir temporaryDirectory(
        QDir::tempPath() +
        QString::fromUtf8(
            "/jxqy 输出安全-XXXXXX"));
    if (!check(
            temporaryDirectory.isValid(),
            "create output-safety temporary root"))
    {
        return false;
    }

    const QString sessionsBase =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("sessions"));
    const QString resourceRoot =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("resources"));
    const QString outsideFile =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("outside.bin"));
    bool ok = check(
        createPrivateDirectory(sessionsBase) &&
            QDir().mkpath(resourceRoot) &&
            createResourceFixture(resourceRoot) &&
            writeFile(
                outsideFile,
                QByteArray("OUTSIDE")),
        "create output-safety fixtures");
    if (!ok)
    {
        return false;
    }

    bool collisionAttempted = false;
    bool collisionCreated = false;
    setDesktopRunSessionWorkspaceFaultInjectorForTests(
        [&collisionAttempted,
         &collisionCreated,
         outsideFile](
            DesktopRunSessionWorkspaceFaultPoint point,
            const QString& path)
        {
            if (collisionAttempted ||
                point !=
                    DesktopRunSessionWorkspaceFaultPoint::
                        BeforeAtomicTemporaryOpen ||
                !path.endsWith(
                    QStringLiteral(
                        "resource-routing-contract.json")))
            {
                return false;
            }
            collisionAttempted = true;
            collisionCreated =
                createHardLink(
                    outsideFile,
                    path);
            return false;
        });
    const DesktopRunSessionWorkspaceResult
        collision =
            createDesktopRunSessionWorkspace(
                sessionsBase,
                validDescriptor(resourceRoot),
                resourceRoots(resourceRoot));
    setDesktopRunSessionWorkspaceFaultInjectorForTests(
        {});
    if (collisionCreated)
    {
        ok = check(
            collision.error ==
                    DesktopRunSessionWorkspaceError::
                        ResourceRoutingContractWriteFailed &&
                !QFileInfo::exists(
                    collision.paths.descriptorPath) &&
                readFile(outsideFile) ==
                    QByteArray("OUTSIDE"),
            "an existing linked output target is rejected without modifying the external file") &&
            ok;
    }
    else
    {
        std::cerr
            << "SKIP: atomic-output hard-link collision is unavailable\n";
        ok = check(
            collisionAttempted &&
                collision.succeeded(),
            "unavailable hard-link collision leaves normal workspace publication intact") &&
            ok;
    }

    DesktopRunSessionWorkspaceResult workspace =
        createDesktopRunSessionWorkspace(
            sessionsBase,
            validDescriptor(resourceRoot),
            resourceRoots(resourceRoot));
    ok = validateWorkspaceTopology(workspace) &&
        ok;
    if (!workspace.succeeded())
    {
        return false;
    }
    const QString realLinkedBase =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("real-linked-base"));
    const QString linkedBase =
        QDir(temporaryDirectory.path()).filePath(
            QStringLiteral("linked-base"));
    if (createPrivateDirectory(realLinkedBase) &&
        createDirectoryRedirect(
            realLinkedBase,
            linkedBase))
    {
        const DesktopRunSessionWorkspaceResult
            linkedBaseResult =
                createDesktopRunSessionWorkspace(
                    linkedBase,
                    validDescriptor(resourceRoot),
                    resourceRoots(resourceRoot));
        ok = check(
            linkedBaseResult.error ==
                    DesktopRunSessionWorkspaceError::
                        SessionsBaseIsLink &&
                linkedBaseResult.sessionId.isEmpty(),
            "the editor-owned sessions base cannot be redirected") &&
            ok;
    }
    else
    {
        std::cerr
            << "SKIP: sessions-base redirect creation is unavailable\n";
    }
    return ok;
}
}

int main()
{
    bool ok = runOpenResourceContractTests();
    ok = runPreparedOverlayTests() && ok;
    ok = runPrivateOutputSafetyTests() && ok;
    setDesktopRunSessionWorkspaceFaultInjectorForTests(
        {});
    return ok ? 0 : 1;
}
