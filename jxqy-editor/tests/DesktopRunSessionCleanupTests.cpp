#include "../core/DesktopRunSessionBase.h"
#include "../core/DesktopRunSessionCleanup.h"
#include "../core/DesktopRunSessionWorkspace.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <optional>

namespace
{
namespace fs = std::filesystem;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

fs::path hostPath(const QString& value)
{
#ifdef Q_OS_WIN
    return fs::path(value.toStdWString());
#else
    return fs::u8path(value.toUtf8().constData());
#endif
}

bool writeFile(const QString& path, const QByteArray& content)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(
               QIODevice::WriteOnly |
               QIODevice::Truncate) &&
        file.write(content) == content.size() &&
        file.flush();
}

struct Fixture
{
    QString sessionsBase;
    QString formalRoot;
    QString formalSentinel;
    DesktopRunSessionWorkspace workspace;
};

std::optional<Fixture> createFixture(const QString& root)
{
    Fixture fixture;
    const DesktopRunSessionBaseResult base =
        ensureDesktopRunSessionBase(
            root,
            QStringLiteral("desktop-run-sessions"));
    if (!base.succeeded())
        return std::nullopt;
    fixture.sessionsBase = base.path;
    fixture.formalRoot =
        QDir(root).filePath(
            QString::fromUtf8("正式资源"));
    fixture.formalSentinel =
        QDir(fixture.formalRoot).filePath(
            QStringLiteral("formal.bin"));
    if (!writeFile(
            fixture.formalSentinel,
            QByteArray("formal-original")))
    {
        return std::nullopt;
    }

    EditorRun::Descriptor descriptor;
    descriptor.assetsCollectionRoot =
        hostPath(fixture.formalRoot);
    descriptor.activeResourcePackId = "JXQY2";
    descriptor.target.sceneId = "cleanup-test";
    descriptor.target.sceneName = u8"清理测试";
    descriptor.target.mapPath = u8"map/cleanup.map";

    DesktopRunSessionFormalRoots roots;
    roots.resourceRoots = {fixture.formalRoot};
    roots.saveRoot = fixture.formalRoot;
    const DesktopRunSessionWorkspaceResult created =
        createDesktopRunSessionWorkspace(
            fixture.sessionsBase,
            descriptor,
            roots);
    if (!created.succeeded())
        return std::nullopt;
    fixture.workspace = created.workspace.value();

    const QString privateOutput =
        QDir(fixture.workspace.paths.overlayRoot).
            filePath(
                QStringLiteral(
                    "nested/private-output.bin"));
    if (!writeFile(
            privateOutput,
            QByteArray("private-output")))
    {
        return std::nullopt;
    }
    return fixture;
}

DesktopRunSessionCleanupRequest requestFor(
    const DesktopRunSessionWorkspace& workspace,
    bool processActive = false)
{
    DesktopRunSessionCleanupRequest request;
    request.sessionId = workspace.sessionId;
    request.processActive = processActive;
    return request;
}

bool successfulCleanupRemovesOnlyCurrentSession()
{
    QTemporaryDir temporary;
    const auto fixture = createFixture(temporary.path());
    if (!check(fixture.has_value(), "fixture must be created"))
        return false;

    const DesktopRunSessionCleanupResult result =
        cleanupDesktopRunSession(
            fixture->sessionsBase,
            fixture->workspace,
            requestFor(fixture->workspace));
    return check(result.succeeded(), "cleanup must succeed") &&
        check(result.removed, "cleanup must report removal") &&
        check(
            !QFileInfo::exists(
                fixture->workspace.paths.sessionRoot),
            "current private session must be removed") &&
        check(
            QFileInfo::exists(fixture->formalSentinel),
            "formal resource must remain");
}

bool activeProcessBlocksCleanup()
{
    QTemporaryDir temporary;
    const auto fixture = createFixture(temporary.path());
    if (!check(fixture.has_value(), "fixture must be created"))
        return false;

    const DesktopRunSessionCleanupResult result =
        cleanupDesktopRunSession(
            fixture->sessionsBase,
            fixture->workspace,
            requestFor(fixture->workspace, true));
    return check(
               result.error ==
                   DesktopRunSessionCleanupError::ProcessActive,
               "active process must block cleanup") &&
        check(
            QFileInfo::exists(
                fixture->workspace.paths.sessionRoot),
            "blocked cleanup must preserve the session");
}

bool mismatchedIdentityBlocksCleanup()
{
    QTemporaryDir temporary;
    const auto fixture = createFixture(temporary.path());
    if (!check(fixture.has_value(), "fixture must be created"))
        return false;

    DesktopRunSessionCleanupRequest request =
        requestFor(fixture->workspace);
    request.sessionId =
        QStringLiteral(
            "00000000-0000-4000-8000-000000000001");
    const DesktopRunSessionCleanupResult result =
        cleanupDesktopRunSession(
            fixture->sessionsBase,
            fixture->workspace,
            request);
    return check(
               result.error ==
                   DesktopRunSessionCleanupError::WorkspaceMismatch,
               "mismatched workspace identity must be rejected") &&
        check(
            QFileInfo::exists(
                fixture->workspace.paths.sessionRoot),
            "identity rejection must preserve the session");
}

bool changedMarkerBlocksCleanup()
{
    QTemporaryDir temporary;
    const auto fixture = createFixture(temporary.path());
    if (!check(fixture.has_value(), "fixture must be created"))
        return false;
    if (!check(
            writeFile(
                fixture->workspace.paths.markerPath,
                QByteArray("{}")),
            "marker must be changed"))
    {
        return false;
    }

    const DesktopRunSessionCleanupResult result =
        cleanupDesktopRunSession(
            fixture->sessionsBase,
            fixture->workspace,
            requestFor(fixture->workspace));
    return check(
               result.error ==
                   DesktopRunSessionCleanupError::MarkerInvalid,
               "changed marker must block cleanup") &&
        check(
            QFileInfo::exists(
                fixture->workspace.paths.sessionRoot),
            "marker rejection must preserve the session");
}

bool alreadyAbsentIsSuccessful()
{
    QTemporaryDir temporary;
    const auto fixture = createFixture(temporary.path());
    if (!check(fixture.has_value(), "fixture must be created"))
        return false;
    const DesktopRunSessionCleanupRequest request =
        requestFor(fixture->workspace);
    const DesktopRunSessionCleanupResult first =
        cleanupDesktopRunSession(
            fixture->sessionsBase,
            fixture->workspace,
            request);
    const DesktopRunSessionCleanupResult second =
        cleanupDesktopRunSession(
            fixture->sessionsBase,
            fixture->workspace,
            request);
    return check(first.succeeded(), "first cleanup must succeed") &&
        check(second.succeeded(), "absent cleanup must succeed") &&
        check(
            second.alreadyAbsent,
            "absent cleanup must be reported as idempotent");
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const bool ok =
        successfulCleanupRemovesOnlyCurrentSession() &&
        activeProcessBlocksCleanup() &&
        mismatchedIdentityBlocksCleanup() &&
        changedMarkerBlocksCleanup() &&
        alreadyAbsentIsSuccessful();
    return ok ? 0 : 1;
}
