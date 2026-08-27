#pragma once

#include "GameProfile.h"

#include <QString>
#include <QStringList>
#include <QList>

#include <atomic>
#include <functional>
#include <memory>

enum class AndroidExternalResourceExportAction
{
    Copy,
    Skip,
    Fail
};

struct AndroidExternalResourceExportEntry
{
    AndroidExternalResourceExportAction action =
        AndroidExternalResourceExportAction::Copy;
    QString sourcePath;
    QString outputPath;
    bool directory = false;
    qint64 size = 0;
    QString sha256;
    QString reason;
};

struct AndroidExternalResourceExportReport
{
    int schemaVersion = 1;
    QString resourcePackId;
    QString resourcePackName;
    QString resourcePackEntryKey;
    QString sourceCollectionRoot;
    QString sourcePackRoot;
    QString bundleDirectory;
    QString installedManifestPath;
    QString retainedBackupPath;
    QStringList warnings;
    QList<AndroidExternalResourceExportEntry> entries;

    int copiedCount() const;
    int skippedCount() const;
    int failedCount() const;
};

enum class AndroidExternalResourceExportStatus
{
    Success,
    Failed,
    Cancelled
};

struct AndroidExternalResourceExportResult
{
    AndroidExternalResourceExportStatus status =
        AndroidExternalResourceExportStatus::Failed;
    QString errorMessage;
    AndroidExternalResourceExportReport report;

    bool succeeded() const
    {
        return status ==
            AndroidExternalResourceExportStatus::Success;
    }
};

struct AndroidExternalResourceExportOptions
{
    // The caller passes the collection and active pack returned by the shared
    // ResourcePackScanner. The packager does not parse another manifest or
    // perform content/reference validation.
    QString collectionRoot;
    ResourcePackInfo activePack;
    QString bundleDirectory;
    std::shared_ptr<std::atomic_bool> cancellationRequested;
    std::function<void(int current, int total,
                       const QString& sourcePath)> progressCallback;
};

class AndroidExternalResourcePackager
{
public:
    enum class FaultPoint
    {
        AfterStaging,
        AfterBackup,
        AfterPublish
    };

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
    using FaultInjector =
        std::function<bool(FaultPoint point)>;
#endif

    // Synchronously exports a directory bundle. GUI callers run this method
    // on an exit-protected worker thread.
    static AndroidExternalResourceExportResult exportBundle(
        const AndroidExternalResourceExportOptions& options);

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
    static void setFaultInjectorForTests(
        FaultInjector injector);
#endif
};
