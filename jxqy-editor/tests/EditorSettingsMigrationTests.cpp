#include "../core/EditorSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
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

bool migrationCopiesOnceWithoutRewriting()
{
    QTemporaryDir temporary;
    if (!check(
            temporary.isValid(),
            "create configuration migration directory"))
    {
        return false;
    }

    const QString legacyPath =
        QDir(temporary.path()).filePath(
            QStringLiteral("legacy/editor_config.ini"));
    const QString targetPath =
        QDir(temporary.path()).filePath(
            QStringLiteral(
                "用户配置/JXQY Editor/editor_config.ini"));
    const QByteArray legacyBytes =
        QString::fromUtf8(
            "[General]\n"
            "theme=深色\n"
            "language=zh_CN\n"
            "desktopRun/gameExecutable=D:/游戏/jxqy.exe\n")
            .toUtf8();
    if (!check(
            writeFile(legacyPath, legacyBytes),
            "write legacy editor configuration"))
    {
        return false;
    }

    const EditorSettings::ConfigurationMigrationResult
        migrated =
            EditorSettings::migrateLegacyConfiguration(
                legacyPath,
                targetPath);
    bool ok = check(
        migrated.succeeded() &&
            migrated.status ==
                EditorSettings::
                    ConfigurationMigrationStatus::
                        Migrated &&
            readFile(targetPath) == legacyBytes,
        "first migration atomically preserves legacy configuration bytes");

    const QByteArray targetGeneration(
        "[General]\ntheme=light\n");
    ok = check(
             writeFile(targetPath, targetGeneration),
             "write newer user configuration generation") &&
        ok;
    ok = check(
             writeFile(
                 legacyPath,
                 QByteArray(
                     "[General]\ntheme=obsolete\n")),
             "change legacy configuration after migration") &&
        ok;
    const EditorSettings::ConfigurationMigrationResult
        repeated =
            EditorSettings::migrateLegacyConfiguration(
                legacyPath,
                targetPath);
    ok = check(
             repeated.succeeded() &&
                 repeated.status ==
                     EditorSettings::
                         ConfigurationMigrationStatus::
                             NotNeeded &&
                 readFile(targetPath) ==
                     targetGeneration,
             "repeat migration never overwrites the user-directory generation") &&
        ok;
    return ok;
}

bool missingSourceAndInvalidTargetAreExplicit()
{
    QTemporaryDir temporary;
    if (!check(
            temporary.isValid(),
            "create configuration edge-case directory"))
    {
        return false;
    }

    const QString missingLegacy =
        QDir(temporary.path()).filePath(
            QStringLiteral("missing/editor_config.ini"));
    const QString targetPath =
        QDir(temporary.path()).filePath(
            QStringLiteral("new/config/editor_config.ini"));
    const EditorSettings::ConfigurationMigrationResult
        absent =
            EditorSettings::migrateLegacyConfiguration(
                missingLegacy,
                targetPath);
    bool ok = check(
        absent.succeeded() &&
            absent.status ==
                EditorSettings::
                    ConfigurationMigrationStatus::
                        NotNeeded &&
            QDir(
                QFileInfo(targetPath).absolutePath())
                .exists() &&
            !QFileInfo::exists(targetPath),
        "missing legacy configuration prepares the writable directory without inventing settings");

    const QString legacyPath =
        QDir(temporary.path()).filePath(
            QStringLiteral("legacy/editor_config.ini"));
    const QString directoryTarget =
        QDir(temporary.path()).filePath(
            QStringLiteral("directory-target"));
    ok = check(
             writeFile(
                 legacyPath,
                 QByteArray("[General]\ntheme=dark\n")) &&
                 QDir().mkpath(directoryTarget),
             "prepare invalid configuration target fixture") &&
        ok;
    const EditorSettings::ConfigurationMigrationResult
        invalidTarget =
            EditorSettings::migrateLegacyConfiguration(
                legacyPath,
                directoryTarget);
    ok = check(
             !invalidTarget.succeeded() &&
                 invalidTarget.status ==
                     EditorSettings::
                         ConfigurationMigrationStatus::
                             Failed &&
                 !invalidTarget.errorMessage.isEmpty() &&
                 readFile(legacyPath) ==
                     QByteArray(
                         "[General]\ntheme=dark\n"),
             "invalid target fails closed and preserves legacy configuration") &&
        ok;
    return ok;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    bool ok = true;
    ok = migrationCopiesOnceWithoutRewriting() && ok;
    ok = missingSourceAndInvalidTargetAreExplicit() && ok;
    if (ok)
        std::cout << "Editor settings migration tests passed\n";
    return ok ? 0 : 1;
}
