#include "EditorSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace
{
bool sameConfigurationPath(
    const QString& left,
    const QString& right)
{
    return QDir::cleanPath(
               QFileInfo(left).absoluteFilePath())
        .compare(
            QDir::cleanPath(
                QFileInfo(right).absoluteFilePath()),
#ifdef Q_OS_WIN
            Qt::CaseInsensitive
#else
            Qt::CaseSensitive
#endif
            ) == 0;
}

void appendUniqueDirectory(
    QStringList& directories,
    const QString& directory)
{
    if (directory.trimmed().isEmpty())
        return;
    const QString normalized =
        QDir::cleanPath(
            QFileInfo(directory).absoluteFilePath());
    if (!directories.contains(
            normalized,
#ifdef Q_OS_WIN
            Qt::CaseInsensitive
#else
            Qt::CaseSensitive
#endif
            ))
    {
        directories.append(normalized);
    }
}

QStringList desktopGameExecutableNames(bool debug)
{
#ifdef Q_OS_WIN
    return {
        debug
        ? QStringLiteral("jxqy-all-in-one-debug.exe")
        : QStringLiteral("jxqy-all-in-one.exe")
    };
#elif defined(Q_OS_MACOS)
    return debug
        ? QStringList{
              QStringLiteral("jxqy-all-in-one-debug.app"),
              QStringLiteral("jxqy-all-in-one-debug")
          }
        : QStringList{
              QStringLiteral("jxqy-all-in-one.app"),
              QStringLiteral("jxqy-all-in-one")
          };
#else
    return {
        debug
        ? QStringLiteral("jxqy-all-in-one-debug")
        : QStringLiteral("jxqy-all-in-one")
    };
#endif
}

QStringList defaultDesktopGameExecutableSearchDirectories(
    const QString& applicationDirectory)
{
    QStringList directories;
    appendUniqueDirectory(
        directories,
        applicationDirectory);
    appendUniqueDirectory(
        directories,
        QDir(applicationDirectory)
            .filePath(QStringLiteral("Release")));

    QDir repositoryCandidate(applicationDirectory);
    QString repositoryRoot;
    for (int depth = 0; depth < 6; ++depth)
    {
        if (QFileInfo(
                repositoryCandidate.filePath(
                    QStringLiteral("CMakeLists.txt")))
                .isFile() &&
            QDir(repositoryCandidate.filePath(
                     QStringLiteral("jxqy-editor")))
                .exists())
        {
            repositoryRoot =
                repositoryCandidate.absolutePath();
            break;
        }
        if (!repositoryCandidate.cdUp())
            break;
    }
    if (repositoryRoot.isEmpty())
        return directories;

#ifdef Q_OS_WIN
    const QString primaryArchitectureDirectory =
        sizeof(void*) == 8
        ? QStringLiteral("win64")
        : QStringLiteral("win32");
    const QString fallbackArchitectureDirectory =
        sizeof(void*) == 8
        ? QStringLiteral("win32")
        : QStringLiteral("win64");
    for (const QString& architectureDirectory :
         {primaryArchitectureDirectory,
          fallbackArchitectureDirectory})
    {
        const QString runtimeDirectory =
            QDir(repositoryRoot).filePath(
                QStringLiteral("bin/%1")
                    .arg(architectureDirectory));
        appendUniqueDirectory(
            directories,
            QDir(runtimeDirectory)
                .filePath(QStringLiteral("Release")));
        appendUniqueDirectory(
            directories,
            runtimeDirectory);
    }
#elif defined(Q_OS_MACOS)
    const QString runtimeDirectory =
        QDir(repositoryRoot).filePath(
            QStringLiteral("bin/macos"));
    appendUniqueDirectory(
        directories,
        QDir(runtimeDirectory)
            .filePath(QStringLiteral("Release")));
    appendUniqueDirectory(
        directories,
        runtimeDirectory);
#else
    const QString runtimeDirectory =
        QDir(repositoryRoot).filePath(
            QStringLiteral("bin/linux"));
    appendUniqueDirectory(
        directories,
        QDir(runtimeDirectory)
            .filePath(QStringLiteral("Release")));
    appendUniqueDirectory(
        directories,
        runtimeDirectory);
#endif
    return directories;
}

QString normalizedExecutablePath(const QString& path)
{
    return QDir::cleanPath(
        QFileInfo(path.trimmed()).absoluteFilePath());
}

#ifdef Q_OS_MACOS
EditorSettings::DesktopExecutableValidation resolveMacBundle(
    const QFileInfo& bundle)
{
    const QDir bundleDirectory(bundle.absoluteFilePath());
    const QString informationPath = bundleDirectory.filePath(
        QStringLiteral("Contents/Info.plist"));
    QSettings bundleInformation(
        informationPath,
        QSettings::NativeFormat);
    const QString executableName =
        bundleInformation.value(
            QStringLiteral("CFBundleExecutable"))
            .toString()
            .trimmed();
    if (bundleInformation.status() != QSettings::NoError ||
        executableName.isEmpty() ||
        executableName == QStringLiteral(".") ||
        executableName == QStringLiteral("..") ||
        executableName.contains('/') ||
        executableName.contains('\\'))
    {
        return {
            EditorSettings::DesktopExecutableError::
                BundleExecutableMissing,
            {}
        };
    }
    return EditorSettings::validateDesktopGameExecutable(
        bundleDirectory.filePath(
            QStringLiteral("Contents/MacOS/%1")
                .arg(executableName)));
}
#endif
}

QString EditorSettings::userConfigurationFilePath()
{
    QString configurationDirectory =
        QStandardPaths::writableLocation(
            QStandardPaths::AppConfigLocation);
    if (configurationDirectory.isEmpty())
    {
        configurationDirectory =
            QDir::home().filePath(
                QStringLiteral(".jxqy-editor"));
    }
    return QDir(configurationDirectory).filePath(
        QStringLiteral("editor_config.ini"));
}

EditorSettings::ConfigurationMigrationResult
EditorSettings::migrateLegacyConfiguration(
    const QString& legacyPath,
    const QString& targetPath)
{
    ConfigurationMigrationResult result;
    result.targetPath = QDir::cleanPath(
        QFileInfo(targetPath).absoluteFilePath());
    if (legacyPath.trimmed().isEmpty() ||
        targetPath.trimmed().isEmpty())
    {
        result.status =
            ConfigurationMigrationStatus::Failed;
        result.errorMessage =
            QStringLiteral(
                "Legacy or target configuration path is empty");
        return result;
    }

    const QString normalizedLegacyPath =
        QDir::cleanPath(
            QFileInfo(legacyPath).absoluteFilePath());
    if (sameConfigurationPath(
            normalizedLegacyPath,
            result.targetPath))
    {
        return result;
    }

    const QFileInfo targetInformation(
        result.targetPath);
    if (targetInformation.exists())
    {
        if (!targetInformation.isFile())
        {
            result.status =
                ConfigurationMigrationStatus::Failed;
            result.errorMessage =
                QStringLiteral(
                    "Target configuration path is not a file: %1")
                    .arg(result.targetPath);
        }
        return result;
    }

    const QFileInfo legacyInformation(
        normalizedLegacyPath);
    if (!legacyInformation.exists())
    {
        const QString targetDirectory =
            QFileInfo(result.targetPath).absolutePath();
        if (!QDir().mkpath(targetDirectory))
        {
            result.status =
                ConfigurationMigrationStatus::Failed;
            result.errorMessage =
                QStringLiteral(
                    "Cannot create user configuration directory: %1")
                    .arg(targetDirectory);
        }
        return result;
    }
    if (!legacyInformation.isFile())
    {
        result.status =
            ConfigurationMigrationStatus::Failed;
        result.errorMessage =
            QStringLiteral(
                "Legacy configuration path is not a file: %1")
                .arg(normalizedLegacyPath);
        return result;
    }

    const QString targetDirectory =
        QFileInfo(result.targetPath).absolutePath();
    if (!QDir().mkpath(targetDirectory))
    {
        result.status =
            ConfigurationMigrationStatus::Failed;
        result.errorMessage =
            QStringLiteral(
                "Cannot create user configuration directory: %1")
                .arg(targetDirectory);
        return result;
    }

    QLockFile migrationLock(
        result.targetPath +
        QStringLiteral(".migration.lock"));
    if (!migrationLock.tryLock(5000))
    {
        if (QFileInfo::exists(result.targetPath))
            return result;
        result.status =
            ConfigurationMigrationStatus::Failed;
        result.errorMessage =
            QStringLiteral(
                "Cannot acquire configuration migration lock: %1")
                .arg(
                    static_cast<int>(
                        migrationLock.error()));
        return result;
    }
    if (QFileInfo::exists(result.targetPath))
        return result;

    QFile source(normalizedLegacyPath);
    if (!source.open(QIODevice::ReadOnly))
    {
        result.status =
            ConfigurationMigrationStatus::Failed;
        result.errorMessage =
            QStringLiteral(
                "Cannot read legacy configuration %1: %2")
                .arg(
                    normalizedLegacyPath,
                    source.errorString());
        return result;
    }

    QSaveFile target(result.targetPath);
    if (!target.open(QIODevice::WriteOnly))
    {
        result.status =
            ConfigurationMigrationStatus::Failed;
        result.errorMessage =
            QStringLiteral(
                "Cannot create user configuration %1: %2")
                .arg(
                    result.targetPath,
                    target.errorString());
        return result;
    }
    while (!source.atEnd())
    {
        const QByteArray chunk = source.read(64 * 1024);
        if (chunk.isEmpty() && source.error() != QFile::NoError)
        {
            target.cancelWriting();
            result.status =
                ConfigurationMigrationStatus::Failed;
            result.errorMessage =
                QStringLiteral(
                    "Cannot finish reading legacy configuration %1: %2")
                    .arg(
                        normalizedLegacyPath,
                        source.errorString());
            return result;
        }
        if (target.write(chunk) != chunk.size())
        {
            target.cancelWriting();
            result.status =
                ConfigurationMigrationStatus::Failed;
            result.errorMessage =
                QStringLiteral(
                    "Cannot copy legacy configuration to %1: %2")
                    .arg(
                        result.targetPath,
                        target.errorString());
            return result;
        }
    }
    if (!target.commit())
    {
        result.status =
            ConfigurationMigrationStatus::Failed;
        result.errorMessage =
            QStringLiteral(
                "Cannot publish migrated configuration %1: %2")
                .arg(
                    result.targetPath,
                    target.errorString());
        return result;
    }
    result.status =
        ConfigurationMigrationStatus::Migrated;
    return result;
}

QSettings EditorSettings::create()
{
    QString path;
    if (qApp)
        path = qApp->property("configFilePath").toString();
    if (path.isEmpty())
    {
        path = userConfigurationFilePath();
        QDir().mkpath(
            QFileInfo(path).absolutePath());
    }
    return QSettings(path, QSettings::IniFormat);
}

EditorSettings::DesktopExecutableValidation
EditorSettings::validateDesktopGameExecutable(
    const QString& selectedPath)
{
    if (selectedPath.trimmed().isEmpty())
        return {DesktopExecutableError::NotSet, {}};

    const QString normalized =
        normalizedExecutablePath(selectedPath);
    const QFileInfo information(normalized);
    if (!information.exists())
        return {DesktopExecutableError::DoesNotExist, normalized};
#ifdef Q_OS_MACOS
    if (information.isDir() &&
        information.fileName().endsWith(
            QStringLiteral(".app"),
            Qt::CaseInsensitive))
    {
        return resolveMacBundle(information);
    }
#endif
    if (information.isDir())
        return {DesktopExecutableError::IsDirectory, normalized};
    const QString canonicalPath = information.canonicalFilePath();
    if (canonicalPath.isEmpty())
        return {DesktopExecutableError::DoesNotExist, normalized};
    const QFileInfo finalTarget(canonicalPath);
    if (!finalTarget.isFile())
        return {DesktopExecutableError::NotRegularFile, normalized};
    if (!finalTarget.isExecutable())
        return {DesktopExecutableError::NotExecutable, normalized};
    return {
        DesktopExecutableError::None,
        normalizedExecutablePath(canonicalPath)
    };
}

EditorSettings::DesktopExecutableValidation
EditorSettings::discoverDesktopGameExecutable(
    const QStringList& searchDirectories)
{
    // Release is the distributable users normally receive. Search every
    // configured directory for it before accepting any Debug fallback.
    for (const bool debug : {false, true})
    {
        const QStringList names =
            desktopGameExecutableNames(debug);
        for (const QString& directory :
             searchDirectories)
        {
            for (const QString& name : names)
            {
                const DesktopExecutableValidation
                    validation =
                        validateDesktopGameExecutable(
                            QDir(directory).filePath(name));
                if (validation.succeeded())
                    return validation;
            }
        }
    }
    return {DesktopExecutableError::NotSet, {}};
}

EditorSettings::DesktopExecutableValidation
EditorSettings::readDesktopGameExecutable(
    QSettings& settings,
    const QStringList& fallbackSearchDirectories)
{
    const DesktopExecutableValidation configured =
        validateDesktopGameExecutable(
            settings.value(
                QString::fromLatin1(
                    DesktopGameExecutableKey))
                .toString());
    if (configured.succeeded())
        return configured;

    const DesktopExecutableValidation discovered =
        discoverDesktopGameExecutable(
            fallbackSearchDirectories);
    return discovered.succeeded()
        ? discovered
        : configured;
}

EditorSettings::DesktopExecutableValidation
EditorSettings::discoverDefaultDesktopGameExecutable(
    const QString& applicationDirectory)
{
    return discoverDesktopGameExecutable(
        defaultDesktopGameExecutableSearchDirectories(
            applicationDirectory));
}

EditorSettings::DesktopExecutableValidation
EditorSettings::readDesktopGameExecutable(QSettings& settings)
{
    return readDesktopGameExecutable(
        settings,
        defaultDesktopGameExecutableSearchDirectories(
            QCoreApplication::applicationDirPath()));
}

bool EditorSettings::writeDesktopGameExecutable(
    QSettings& settings,
    const QString& selectedPath,
    QString* errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    const DesktopExecutableValidation validation =
        validateDesktopGameExecutable(selectedPath);
    if (!validation.succeeded())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral(
                "desktop executable validation failed: %1")
                .arg(static_cast<int>(validation.error));
        }
        return false;
    }

    const QString key =
        QString::fromLatin1(DesktopGameExecutableKey);
    const bool hadPreviousValue = settings.contains(key);
    const QVariant previousValue = settings.value(key);
    settings.setValue(key, validation.executablePath);
    settings.sync();
    if (settings.status() == QSettings::NoError &&
        settings.value(key).toString() ==
            validation.executablePath)
    {
        return true;
    }

    if (hadPreviousValue)
        settings.setValue(key, previousValue);
    else
        settings.remove(key);
    settings.sync();
    if (errorMessage)
    {
        *errorMessage =
            QStringLiteral("desktop executable setting write failed");
    }
    return false;
}
