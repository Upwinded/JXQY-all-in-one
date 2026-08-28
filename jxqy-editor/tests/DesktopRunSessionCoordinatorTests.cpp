#include "../core/DesktopRunSessionCoordinator.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#ifdef Q_OS_WIN
#include <Aclapi.h>
#include <Windows.h>
#endif

#include <functional>
#include <iostream>
#include <optional>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(
               QIODevice::WriteOnly |
               QIODevice::Truncate) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

#ifdef Q_OS_WIN
bool setPrivateDirectoryAcl(const QString& path)
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
    QByteArray tokenStorage(
        static_cast<qsizetype>(tokenBytes),
        Qt::Uninitialized);
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

    EXPLICIT_ACCESSW access = {};
    access.grfAccessPermissions = FILE_ALL_ACCESS;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance =
        SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName =
        static_cast<LPWSTR>(
            reinterpret_cast<TOKEN_USER*>(
                tokenStorage.data())->User.Sid);
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
        LocalFree(dacl);
    CloseHandle(token);
    return status == ERROR_SUCCESS;
}
#endif

bool createPrivateDirectory(const QString& path)
{
    if (!QDir().mkpath(path))
        return false;
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

bool waitUntil(
    const std::function<bool()>& predicate,
    int timeoutMilliseconds = 30000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() &&
           timer.elapsed() < timeoutMilliseconds)
    {
        QCoreApplication::processEvents(
            QEventLoop::AllEvents,
            20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(
        QEventLoop::AllEvents,
        20);
    return predicate();
}

std::optional<DesktopRunSessionRequest> createRequest(
    const QString& fixturePath,
    const QString& sessionsBase,
    const QString& formalRoot,
    const QString& mode)
{
    if (!writeFile(
            QDir(formalRoot).filePath(
                QString::fromUtf8("map/中都.map")),
            QByteArray("map")) ||
        !writeFile(
            QDir(formalRoot).filePath(
                QString::fromUtf8("script/入口.txt")),
            QByteArray("return")))
    {
        return std::nullopt;
    }

    PreparedSavedSceneLaunch prepared;
    prepared.scene.id =
        QStringLiteral("fixture-") + mode;
    prepared.scene.name =
        QString::fromUtf8("协调器场景");
    prepared.scene.mapPath =
        QString::fromUtf8("map/中都.map");
    prepared.scene.entryScriptPath =
        QString::fromUtf8("script/入口.txt");
    prepared.scene.playerPosition = QPoint(12, 34);
    prepared.assetsCollectionRoot = formalRoot;
    prepared.canonicalActiveResourcePackId =
        QStringLiteral("JXQY2");
    prepared.activeContentRoot = formalRoot;
    prepared.formalRoots = {formalRoot};

    DesktopRunSessionRequest request;
    request.executablePath = fixturePath;
    request.trustedSessionsBaseDirectory =
        sessionsBase;
    request.preparedLaunch = std::move(prepared);
    return request;
}

bool runCurrentSessionLifecycle(const QString& fixturePath)
{
    QTemporaryDir temporary;
    const QString sessionsBase =
        QDir(temporary.path()).filePath(
            QStringLiteral("sessions"));
    const QString firstFormalRoot =
        QDir(temporary.path()).filePath(
            QStringLiteral("formal-success"));
    const QString secondFormalRoot =
        QDir(temporary.path()).filePath(
            QStringLiteral("formal-nonzero"));
    if (!check(
            temporary.isValid() &&
                createPrivateDirectory(sessionsBase),
            "create lifecycle roots"))
    {
        return false;
    }

    const auto firstRequest = createRequest(
        fixturePath,
        sessionsBase,
        firstFormalRoot,
        QStringLiteral("success"));
    const auto secondRequest = createRequest(
        fixturePath,
        sessionsBase,
        secondFormalRoot,
        QStringLiteral("nonzero"));
    if (!firstRequest || !secondRequest)
        return false;

    DesktopRunSessionCoordinator coordinator;
    QString standardOutput;
    QString standardError;
    QObject::connect(
        &coordinator,
        &DesktopRunSessionCoordinator::standardOutputReceived,
        [&](const QString& text)
        {
            standardOutput += text;
        });
    QObject::connect(
        &coordinator,
        &DesktopRunSessionCoordinator::standardErrorReceived,
        [&](const QString& text)
        {
            standardError += text;
        });

    bool ok = check(
        coordinator.start(*firstRequest),
        "start first current session");
    ok = check(
             waitUntil(
                 [&]()
                 {
                     return coordinator.state() ==
                         DesktopRunSessionCoordinatorState::Terminal;
                 }),
             "first run reaches terminal") &&
        ok;
    const DesktopRunSessionTerminalResult first =
        coordinator.terminalResult();
    const auto firstPresentation =
        coordinator.currentSessionPresentation();
    ok = check(
             first.succeeded() &&
                 coordinator.hasCurrentSession() &&
                 firstPresentation.has_value() &&
                 firstPresentation->sessionId == first.sessionId &&
                 QFileInfo::exists(first.sessionPath) &&
                 standardOutput.contains(
                     QString::fromUtf8("标准输出：中文分片")) &&
                 standardError.contains(
                     QString::fromUtf8("错误输出：中文分片")),
             "finished current session remains visible in this invocation") &&
        ok;

    ok = check(
             coordinator.start(*secondRequest),
             "start second current session") &&
        ok;
    ok = check(
             !QFileInfo::exists(first.sessionPath),
             "starting a new run removes the previous session") &&
        ok;
    ok = check(
             waitUntil(
                 [&]()
                 {
                     return coordinator.state() ==
                         DesktopRunSessionCoordinatorState::Terminal;
                 }),
             "second run reaches terminal") &&
        ok;
    const DesktopRunSessionTerminalResult second =
        coordinator.terminalResult();
    ok = check(
             second.outcome ==
                     DesktopRunSessionCoordinatorOutcome::NonZeroExit &&
                 second.exitCode == 70 &&
                 QFileInfo::exists(second.sessionPath),
             "second run replaces current diagnostics") &&
        ok;

    coordinator.prepareForApplicationExit();
    ok = check(
             !coordinator.hasCurrentSession() &&
                 !QFileInfo::exists(second.sessionPath),
             "application exit removes the current finished session") &&
        ok;
    ok = check(
             QFileInfo::exists(
                 QDir(firstFormalRoot).filePath(
                     QString::fromUtf8("map/中都.map"))) &&
                 QFileInfo::exists(
                     QDir(secondFormalRoot).filePath(
                         QString::fromUtf8("map/中都.map"))),
             "session cleanup preserves formal resources") &&
        ok;
    return ok;
}

bool runStopLifecycle(const QString& fixturePath)
{
    QTemporaryDir temporary;
    const QString sessionsBase =
        QDir(temporary.path()).filePath(
            QStringLiteral("sessions"));
    const QString formalRoot =
        QDir(temporary.path()).filePath(
            QStringLiteral("formal-hang"));
    if (!createPrivateDirectory(sessionsBase))
        return false;
    const auto request = createRequest(
        fixturePath,
        sessionsBase,
        formalRoot,
        QStringLiteral("hang"));
    if (!request)
        return false;

    DesktopRunSessionCoordinator coordinator;
    if (!check(coordinator.start(*request), "start hanging run") ||
        !check(
            waitUntil(
                [&]()
                {
                    return coordinator.state() ==
                        DesktopRunSessionCoordinatorState::Running;
                }),
            "hanging run reaches running state"))
    {
        return false;
    }
    coordinator.setStopTimeoutMilliseconds(50);
    coordinator.requestStop();
    if (!check(
            waitUntil(
                [&]()
                {
                    return coordinator.state() ==
                        DesktopRunSessionCoordinatorState::Terminal;
                }),
            "stopped run reaches terminal"))
    {
        return false;
    }
    const DesktopRunSessionTerminalResult terminal =
        coordinator.terminalResult();
    const bool ok = check(
        terminal.outcome ==
                DesktopRunSessionCoordinatorOutcome::StoppedByUser &&
            QFileInfo::exists(terminal.sessionPath),
        "stopped run is the current visible session");
    coordinator.prepareForApplicationExit();
    return check(
               !QFileInfo::exists(terminal.sessionPath),
               "stopped current session is removed on exit") &&
        ok;
}

bool runActiveExitCleanup(const QString& fixturePath)
{
    QTemporaryDir temporary;
    const QString sessionsBase =
        QDir(temporary.path()).filePath(
            QStringLiteral("sessions"));
    const QString formalRoot =
        QDir(temporary.path()).filePath(
            QStringLiteral("formal-active-exit"));
    if (!createPrivateDirectory(sessionsBase))
        return false;
    const auto request = createRequest(
        fixturePath,
        sessionsBase,
        formalRoot,
        QStringLiteral("hang"));
    if (!request)
        return false;

    DesktopRunSessionCoordinator coordinator;
    if (!coordinator.start(*request) ||
        !waitUntil(
            [&]()
            {
                return coordinator.state() ==
                    DesktopRunSessionCoordinatorState::Running;
            }))
    {
        return false;
    }
    const auto presentation =
        coordinator.currentSessionPresentation();
    if (!presentation)
        return false;
    const QString sessionPath =
        presentation->paths.sessionRoot;
    QElapsedTimer elapsed;
    elapsed.start();
    coordinator.prepareForApplicationExit();
    return check(
               elapsed.elapsed() < 5000,
               "active exit uses one bounded stop") &&
        check(
            !QFileInfo::exists(sessionPath),
            "active exit removes the current private session") &&
        check(
            QFileInfo::exists(
                QDir(formalRoot).filePath(
                    QString::fromUtf8("map/中都.map"))),
            "active exit preserves formal resources");
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QString fixturePath =
        qEnvironmentVariable(
            "JXQY_EDITOR_DESKTOP_RUN_FIXTURE");
    if (!check(
            !fixturePath.isEmpty() &&
                QFileInfo::exists(fixturePath),
            "desktop-run process fixture is available"))
    {
        return 1;
    }

    const bool ok =
        runCurrentSessionLifecycle(fixturePath) &&
        runStopLifecycle(fixturePath) &&
        runActiveExitCleanup(fixturePath);
    return ok ? 0 : 1;
}
