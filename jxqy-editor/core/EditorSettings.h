#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

namespace EditorSettings
{
inline constexpr const char* DesktopGameExecutableKey =
    "desktopRun/gameExecutable";

enum class ConfigurationMigrationStatus
{
    NotNeeded,
    Migrated,
    Failed
};

struct ConfigurationMigrationResult
{
    ConfigurationMigrationStatus status =
        ConfigurationMigrationStatus::NotNeeded;
    QString targetPath;
    QString errorMessage;

    bool succeeded() const
    {
        return status != ConfigurationMigrationStatus::Failed;
    }
};

enum class DesktopExecutableError
{
    None,
    NotSet,
    DoesNotExist,
    IsDirectory,
    NotRegularFile,
    NotExecutable,
    BundleExecutableMissing,
    SettingsWriteFailed
};

struct DesktopExecutableValidation
{
    DesktopExecutableError error = DesktopExecutableError::NotSet;
    QString executablePath;

    bool succeeded() const
    {
        return error == DesktopExecutableError::None;
    }
};

QString userConfigurationFilePath();
ConfigurationMigrationResult migrateLegacyConfiguration(
    const QString& legacyPath,
    const QString& targetPath);
QSettings create();
DesktopExecutableValidation validateDesktopGameExecutable(
    const QString& selectedPath);
DesktopExecutableValidation discoverDesktopGameExecutable(
    const QStringList& searchDirectories);
DesktopExecutableValidation
discoverDefaultDesktopGameExecutable(
    const QString& applicationDirectory);
DesktopExecutableValidation readDesktopGameExecutable(
    QSettings& settings);
DesktopExecutableValidation readDesktopGameExecutable(
    QSettings& settings,
    const QStringList& fallbackSearchDirectories);
bool writeDesktopGameExecutable(
    QSettings& settings,
    const QString& selectedPath,
    QString* errorMessage = nullptr);
}
