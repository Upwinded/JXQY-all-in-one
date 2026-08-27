#include "AndroidExternalResourcePackager.h"

#include "AuthoringMutationGate.h"
#include "DurableFileTransaction.h"
#include "EditorAssetPath.h"
#include "../../src/Resource/ResourceCatalog.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QUuid>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>

namespace
{
constexpr qint64 CopyBufferSize = 1024 * 1024;

struct PlannedEntry
{
    QString sourcePath;
    QString outputRelativePath;
    bool directory = false;
    bool skipped = false;
    qint64 size = 0;
    qint64 lastModifiedMilliseconds = 0;
    int reportEntryIndex = -1;
};

struct CopyRoot
{
    QString sourceRoot;
    QString outputRelativeRoot;
};

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
QMutex faultInjectorMutex;
AndroidExternalResourcePackager::FaultInjector faultInjector;
#endif

QString slashPath(QString path)
{
    path.replace('\\', '/');
    return path;
}

QString lowercaseAsciiPath(QString path)
{
    for (int index = 0; index < path.size(); ++index)
    {
        const ushort character = path.at(index).unicode();
        if (character >= 'A' && character <= 'Z')
        {
            path[index] = QChar(
                static_cast<ushort>(character + ('a' - 'A')));
        }
    }
    return path;
}

QString actionName(AndroidExternalResourceExportAction action)
{
    switch (action)
    {
    case AndroidExternalResourceExportAction::Copy:
        return QStringLiteral("copy");
    case AndroidExternalResourceExportAction::Skip:
        return QStringLiteral("skip");
    case AndroidExternalResourceExportAction::Fail:
        return QStringLiteral("fail");
    }
    return QStringLiteral("fail");
}

bool faultRequested(
    AndroidExternalResourcePackager::FaultPoint point)
{
#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
    QMutexLocker locker(&faultInjectorMutex);
    return faultInjector && faultInjector(point);
#else
    (void)point;
    return false;
#endif
}

bool cancellationRequested(
    const AndroidExternalResourceExportOptions& options)
{
    return options.cancellationRequested &&
        options.cancellationRequested->load(
            std::memory_order_acquire);
}

bool isLinkOrReparsePoint(const QFileInfo& fileInfo)
{
    if (fileInfo.isSymLink())
        return true;
#ifdef Q_OS_WIN
    const QString nativePath =
        QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    const DWORD attributes = GetFileAttributesW(
        reinterpret_cast<LPCWSTR>(nativePath.utf16()));
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}

QString destinationIdentity(const QString& relativePath)
{
    return slashPath(QDir::cleanPath(relativePath))
        .normalized(QString::NormalizationForm_C)
        .toCaseFolded();
}

QString reportedOutputPath(const QString& assetsRelativePath)
{
    return slashPath(QDir::cleanPath(
        QStringLiteral("Download/jxqy/assets/") +
        assetsRelativePath));
}

bool isSafeRelativeOutputPath(
    const QString& path,
    QString& normalized)
{
    normalized = slashPath(QDir::cleanPath(path.trimmed()));
    if (normalized.isEmpty() ||
        normalized == QStringLiteral(".") ||
        QDir::isAbsolutePath(normalized) ||
        normalized == QStringLiteral("..") ||
        normalized.startsWith(QStringLiteral("../")) ||
        normalized.contains(':'))
    {
        return false;
    }
    return true;
}

QString uniqueSiblingPath(
    const QString& targetPath,
    const QString& label)
{
    const QFileInfo targetInfo(targetPath);
    const QDir parent = targetInfo.dir();
    const QString baseName = targetInfo.fileName();
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        const QString token = QUuid::createUuid()
            .toString(QUuid::WithoutBraces);
        const QString candidate = parent.filePath(
            QStringLiteral(".%1.jxqy-%2-%3")
                .arg(baseName, label, token));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}

bool renameSibling(
    const QString& sourcePath,
    const QString& targetPath)
{
    return QDir().rename(sourcePath, targetPath);
}

bool removeGeneratedTree(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;
    if (isLinkOrReparsePoint(info))
        return false;
    if (info.isFile())
        return QFile::remove(path);
    return QDir(path).removeRecursively();
}

bool treeContainsLinkOrReparsePoint(
    const QString& rootPath,
    QString& offendingPath)
{
    const QFileInfo rootInfo(rootPath);
    if (isLinkOrReparsePoint(rootInfo))
    {
        offendingPath = rootPath;
        return true;
    }
    if (!rootInfo.isDir())
        return false;

    const QFileInfoList entries = QDir(rootPath).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System |
            QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& entry : entries)
    {
        if (isLinkOrReparsePoint(entry))
        {
            offendingPath = entry.absoluteFilePath();
            return true;
        }
        if (entry.isDir() &&
            treeContainsLinkOrReparsePoint(
                entry.absoluteFilePath(), offendingPath))
        {
            return true;
        }
    }
    return false;
}

bool pathContainsLinkOrReparsePoint(
    const QString& basePath,
    const QString& candidatePath,
    QString& offendingPath)
{
    if (!EditorAssetPath::isLexicallyInside(basePath, candidatePath))
        return false;

    QString current =
        EditorAssetPath::normalizedAbsolutePath(basePath);
    const QFileInfo baseInfo(current);
    if (isLinkOrReparsePoint(baseInfo))
    {
        offendingPath = current;
        return true;
    }

    const QString relative = slashPath(
        QDir(current).relativeFilePath(candidatePath));
    const QStringList segments = relative.split(
        '/', Qt::SkipEmptyParts);
    for (const QString& segment : segments)
    {
        if (segment == QStringLiteral("."))
            continue;
        current = QDir(current).filePath(segment);
        const QFileInfo info(current);
        if (isLinkOrReparsePoint(info))
        {
            offendingPath = current;
            return true;
        }
    }
    return false;
}

bool existingParentChainContainsLinkOrReparsePoint(
    const QString& path,
    QString& offendingPath)
{
    offendingPath.clear();
    QString current =
        EditorAssetPath::normalizedAbsolutePath(path);
    while (!current.isEmpty())
    {
        const QFileInfo info(current);
        if ((info.exists() || info.isSymLink()) &&
            isLinkOrReparsePoint(info))
        {
            offendingPath = current;
            return true;
        }

        const QString parent = info.dir().absolutePath();
        if (parent.isEmpty() || parent == current)
            break;
        current = parent;
    }
    return false;
}

bool pathsOverlapLexically(
    const QString& left,
    const QString& right)
{
    return EditorAssetPath::isLexicallyInside(left, right) ||
        EditorAssetPath::isLexicallyInside(right, left);
}

bool pathsOverlapAfterCanonicalResolution(
    const QString& left,
    const QString& right)
{
    const QString resolvedLeft =
        EditorAssetPath::comparisonKey(left);
    const QString resolvedRight =
        EditorAssetPath::comparisonKey(right);
    return !resolvedLeft.isEmpty() &&
        !resolvedRight.isEmpty() &&
        pathsOverlapLexically(resolvedLeft, resolvedRight);
}

QString skipReason(
    const QString& relativePath,
    const QFileInfo& fileInfo)
{
    const QString normalized = slashPath(relativePath);
    const QStringList segments = normalized.split(
        '/', Qt::SkipEmptyParts);
    QStringList foldedSegments;
    foldedSegments.reserve(segments.size());
    for (const QString& segment : segments)
        foldedSegments.append(segment.toCaseFolded());

    if (!foldedSegments.isEmpty() && fileInfo.isDir() &&
        foldedSegments.first() == QStringLiteral("save"))
    {
        return QStringLiteral("runtime-save-state");
    }
    if (!foldedSegments.isEmpty() &&
        foldedSegments.first() == QStringLiteral(".git"))
    {
        return QStringLiteral("git-metadata");
    }
    if (!foldedSegments.isEmpty() &&
        foldedSegments.first() == QStringLiteral(".jxqy_editor"))
    {
        return QStringLiteral("editor-local-state");
    }

    const QString name = fileInfo.fileName();
    const QString foldedName = name.toCaseFolded();
    const bool isRootEntry = foldedSegments.size() == 1;
    if (isRootEntry &&
        (foldedName == QStringLiteral(".gitignore") ||
            foldedName == QStringLiteral(".gitattributes")))
    {
        return QStringLiteral("git-metadata");
    }
    if (isRootEntry &&
        foldedName == QStringLiteral(".jxqy_asset_migration_marker"))
    {
        return QStringLiteral("migration-state");
    }
    if (isRootEntry &&
        foldedName.endsWith(QStringLiteral(".jxqyproj")))
    {
        return QStringLiteral("editor-project-file");
    }
    if (foldedName == QStringLiteral(".ds_store") ||
        foldedName == QStringLiteral("thumbs.db"))
    {
        return QStringLiteral("operating-system-metadata");
    }
    static const QStringList generatedReportNames =
    {
        QStringLiteral("migration_report.txt"),
        QStringLiteral("migration_report.json"),
        QStringLiteral("conversion_report.txt"),
        QStringLiteral("conversion_report.json"),
        QStringLiteral("android_external_resource_report.txt"),
        QStringLiteral("android_external_resource_report.json")
    };
    if (isRootEntry && generatedReportNames.contains(foldedName))
    {
        return QStringLiteral("generated-report");
    }
    return {};
}

void appendWarningOnce(
    AndroidExternalResourceExportReport& report,
    const QString& warning)
{
    if (!warning.isEmpty() && !report.warnings.contains(warning))
        report.warnings.append(warning);
}

void markEntryFailed(
    AndroidExternalResourceExportReport& report,
    int index,
    const QString& reason)
{
    if (index < 0 || index >= report.entries.size())
        return;
    report.entries[index].action =
        AndroidExternalResourceExportAction::Fail;
    report.entries[index].reason = reason;
}

bool appendPlannedEntry(
    const QFileInfo& sourceInfo,
    const QString& outputRelativePath,
    const QString& inheritedSkipReason,
    QList<PlannedEntry>& plan,
    QHash<QString, int>& outputOwners,
    AndroidExternalResourceExportReport& report,
    QString& errorMessage)
{
    const QString normalizedOutputRelativePath =
        lowercaseAsciiPath(slashPath(outputRelativePath));
    AndroidExternalResourceExportEntry outcome;
    outcome.sourcePath = sourceInfo.absoluteFilePath();
    outcome.outputPath = reportedOutputPath(
        normalizedOutputRelativePath);
    outcome.directory = sourceInfo.isDir();
    outcome.size = sourceInfo.isFile() ? sourceInfo.size() : 0;

    PlannedEntry entry;
    entry.sourcePath = sourceInfo.absoluteFilePath();
    entry.outputRelativePath = normalizedOutputRelativePath;
    entry.directory = sourceInfo.isDir();
    entry.size = sourceInfo.isFile() ? sourceInfo.size() : 0;
    entry.lastModifiedMilliseconds =
        sourceInfo.lastModified().toMSecsSinceEpoch();
    entry.skipped = !inheritedSkipReason.isEmpty();

    if (entry.skipped)
    {
        outcome.action = AndroidExternalResourceExportAction::Skip;
        outcome.reason = inheritedSkipReason;
        entry.reportEntryIndex = report.entries.size();
        report.entries.append(outcome);
        plan.append(entry);
        return true;
    }

    const QString outputKey =
        destinationIdentity(entry.outputRelativePath);
    const auto owner = outputOwners.constFind(outputKey);
    if (owner != outputOwners.constEnd())
    {
        const PlannedEntry& existing = plan[*owner];
        if (existing.directory == entry.directory &&
            EditorAssetPath::comparisonKey(existing.sourcePath) ==
                EditorAssetPath::comparisonKey(entry.sourcePath))
        {
            return true;
        }
        outcome.action = AndroidExternalResourceExportAction::Fail;
        outcome.reason = QStringLiteral("output-path-collision");
        report.entries.append(outcome);
        errorMessage = QStringLiteral(
            "Multiple source entries map to the same Android output path: %1")
                .arg(entry.outputRelativePath);
        return false;
    }

    entry.reportEntryIndex = report.entries.size();
    report.entries.append(outcome);
    outputOwners.insert(outputKey, plan.size());
    plan.append(entry);
    return true;
}

bool scanCopyRoot(
    const CopyRoot& copyRoot,
    QList<PlannedEntry>& plan,
    QHash<QString, int>& outputOwners,
    AndroidExternalResourceExportReport& report,
    QString& errorMessage)
{
    const QFileInfo rootInfo(copyRoot.sourceRoot);
    if (!rootInfo.exists() || !rootInfo.isDir())
    {
        errorMessage = QStringLiteral(
            "Resource root is not an available directory: %1")
                .arg(copyRoot.sourceRoot);
        return false;
    }
    if (isLinkOrReparsePoint(rootInfo))
    {
        AndroidExternalResourceExportEntry outcome;
        outcome.action = AndroidExternalResourceExportAction::Fail;
        outcome.sourcePath = rootInfo.absoluteFilePath();
        outcome.outputPath = reportedOutputPath(
            copyRoot.outputRelativeRoot);
        outcome.directory = true;
        outcome.reason = QStringLiteral("link-or-reparse-point");
        report.entries.append(outcome);
        errorMessage = QStringLiteral(
            "Links and reparse points are not allowed in an Android export: %1")
                .arg(rootInfo.absoluteFilePath());
        return false;
    }

    if (!appendPlannedEntry(
            rootInfo,
            copyRoot.outputRelativeRoot,
            {},
            plan,
            outputOwners,
            report,
            errorMessage))
    {
        return false;
    }

    std::function<bool(const QString&, const QString&,
                       const QString&, const QString&)>
        visitDirectory;
    visitDirectory =
        [&](const QString& directoryPath,
            const QString& sourceRelativeDirectory,
            const QString& outputRelativeDirectory,
            const QString& inheritedSkipReason)
        {
            const QDir directory(directoryPath);
            if (!directory.isReadable())
            {
                errorMessage = QStringLiteral(
                    "Unable to enumerate source directory: %1")
                        .arg(directoryPath);
                return false;
            }
            const QFileInfoList entries = directory.entryInfoList(
                QDir::AllEntries | QDir::Hidden | QDir::System |
                    QDir::NoDotAndDotDot,
                QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
            for (const QFileInfo& sourceInfo : entries)
            {
                const QString sourceRelativePath =
                    sourceRelativeDirectory.isEmpty()
                    ? sourceInfo.fileName()
                    : sourceRelativeDirectory + '/' +
                        sourceInfo.fileName();
                const QString outputRelativePath =
                    outputRelativeDirectory.isEmpty()
                    ? sourceInfo.fileName()
                    : outputRelativeDirectory + '/' +
                        sourceInfo.fileName();

                if (isLinkOrReparsePoint(sourceInfo))
                {
                    AndroidExternalResourceExportEntry outcome;
                    outcome.action =
                        AndroidExternalResourceExportAction::Fail;
                    outcome.sourcePath = sourceInfo.absoluteFilePath();
                    outcome.outputPath = reportedOutputPath(
                        outputRelativePath);
                    outcome.directory = sourceInfo.isDir();
                    outcome.reason =
                        QStringLiteral("link-or-reparse-point");
                    report.entries.append(outcome);
                    errorMessage = QStringLiteral(
                        "Links and reparse points are not allowed in an Android export: %1")
                            .arg(sourceInfo.absoluteFilePath());
                    return false;
                }

                QString effectiveSkipReason = inheritedSkipReason;
                if (effectiveSkipReason.isEmpty())
                {
                    effectiveSkipReason = skipReason(
                        sourceRelativePath, sourceInfo);
                }
                if (!sourceInfo.isDir() && !sourceInfo.isFile())
                {
                    AndroidExternalResourceExportEntry outcome;
                    outcome.action =
                        AndroidExternalResourceExportAction::Fail;
                    outcome.sourcePath = sourceInfo.absoluteFilePath();
                    outcome.outputPath = reportedOutputPath(
                        outputRelativePath);
                    outcome.reason =
                        QStringLiteral("unsupported-file-system-entry");
                    report.entries.append(outcome);
                    errorMessage = QStringLiteral(
                        "Unsupported file-system entry in Android export: %1")
                            .arg(sourceInfo.absoluteFilePath());
                    return false;
                }
                if (!appendPlannedEntry(
                        sourceInfo,
                        outputRelativePath,
                        effectiveSkipReason,
                        plan,
                        outputOwners,
                        report,
                        errorMessage))
                {
                    return false;
                }
                if (sourceInfo.isDir() &&
                    !visitDirectory(
                        sourceInfo.absoluteFilePath(),
                        sourceRelativePath,
                        outputRelativePath,
                        effectiveSkipReason))
                {
                    return false;
                }
            }
            return true;
        };

    return visitDirectory(
        rootInfo.absoluteFilePath(),
        {},
        copyRoot.outputRelativeRoot,
        {});
}

bool addOptionalCopyRoot(
    const QString& label,
    const QString& configuredPath,
    bool include,
    const QString& collectionRoot,
    const QString& packRoot,
    const QString& packOutputDirectory,
    QList<CopyRoot>& roots,
    AndroidExternalResourceExportReport& report,
    QString& errorMessage)
{
    const QString trimmedPath = configuredPath.trimmed();
    if (trimmedPath.isEmpty())
        return true;
    if (!include)
    {
        appendWarningOnce(
            report,
            QStringLiteral("%1 is declared but was not copied: %2")
                .arg(label, trimmedPath));
        return true;
    }
    if (QDir::isAbsolutePath(trimmedPath) ||
        trimmedPath.startsWith(QStringLiteral("//")) ||
        trimmedPath.contains(':'))
    {
        appendWarningOnce(
            report,
            QStringLiteral("%1 was not copied because it is not a portable relative path: %2")
                .arg(label, trimmedPath));
        return true;
    }

    const QString lowercaseConfiguredPath =
        lowercaseAsciiPath(slashPath(trimmedPath));
    const QString sourcePath =
        EditorAssetPath::normalizedAbsolutePath(
            QDir(packRoot).filePath(lowercaseConfiguredPath));
    QString offendingPath;
    if (pathContainsLinkOrReparsePoint(
            collectionRoot, sourcePath, offendingPath))
    {
        errorMessage = QStringLiteral(
            "%1 crosses a link or reparse point: %2")
                .arg(label, offendingPath);
        return false;
    }
    if (!EditorAssetPath::isInside(collectionRoot, sourcePath))
    {
        appendWarningOnce(
            report,
            QStringLiteral("%1 was not copied because it resolves outside the resource collection: %2")
                .arg(label, sourcePath));
        return true;
    }
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir())
    {
        appendWarningOnce(
            report,
            QStringLiteral("%1 was not copied because the directory is unavailable: %2")
                .arg(label, sourcePath));
        return true;
    }
    if (isLinkOrReparsePoint(sourceInfo))
    {
        errorMessage = QStringLiteral(
            "%1 resolves to a link or reparse point: %2")
                .arg(label, sourcePath);
        return false;
    }
    const QString outputCandidate = slashPath(QDir::cleanPath(
        QDir(packOutputDirectory).filePath(
            lowercaseConfiguredPath)));
    QString outputRelativePath;
    if (outputCandidate == QStringLiteral("."))
    {
        outputRelativePath.clear();
    }
    else if (!isSafeRelativeOutputPath(
                 outputCandidate,
                 outputRelativePath))
    {
        appendWarningOnce(
            report,
            QStringLiteral("%1 was not copied because its Android output path escapes the assets directory: %2")
                .arg(label, trimmedPath));
        return true;
    }

    roots.append({sourcePath, outputRelativePath});
    return true;
}

bool copyFileWithHash(
    const PlannedEntry& entry,
    const QString& targetPath,
    const AndroidExternalResourceExportOptions& options,
    QString& hash,
    QString& errorMessage)
{
    const QFileInfo before(entry.sourcePath);
    if (!before.exists() || !before.isFile() ||
        isLinkOrReparsePoint(before) ||
        before.size() != entry.size ||
        before.lastModified().toMSecsSinceEpoch() !=
            entry.lastModifiedMilliseconds)
    {
        errorMessage = QStringLiteral(
            "Source file changed before it could be copied: %1")
                .arg(entry.sourcePath);
        return false;
    }

    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath()))
    {
        errorMessage = QStringLiteral(
            "Unable to create Android export directory: %1")
                .arg(QFileInfo(targetPath).absolutePath());
        return false;
    }

    QFile source(entry.sourcePath);
    QFile target(targetPath);
    if (!source.open(QIODevice::ReadOnly))
    {
        errorMessage = QStringLiteral("Unable to read source file: %1 (%2)")
            .arg(entry.sourcePath, source.errorString());
        return false;
    }
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        errorMessage = QStringLiteral("Unable to write staged file: %1 (%2)")
            .arg(targetPath, target.errorString());
        return false;
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    while (!source.atEnd())
    {
        if (cancellationRequested(options))
        {
            errorMessage = QStringLiteral("cancelled");
            return false;
        }
        const QByteArray bytes = source.read(CopyBufferSize);
        if (bytes.isEmpty() && source.error() != QFileDevice::NoError)
        {
            errorMessage = QStringLiteral("Unable to read source file: %1 (%2)")
                .arg(entry.sourcePath, source.errorString());
            return false;
        }
        hasher.addData(bytes);
        qint64 written = 0;
        while (written < bytes.size())
        {
            const qint64 count = target.write(
                bytes.constData() + written,
                bytes.size() - written);
            if (count <= 0)
            {
                errorMessage = QStringLiteral("Unable to write staged file: %1 (%2)")
                    .arg(targetPath, target.errorString());
                return false;
            }
            written += count;
        }
    }
    if (!target.flush())
    {
        errorMessage = QStringLiteral("Unable to flush staged file: %1 (%2)")
            .arg(targetPath, target.errorString());
        return false;
    }
    target.close();
    source.close();

    const QFileInfo after(entry.sourcePath);
    const QFileInfo staged(targetPath);
    if (!after.exists() || !after.isFile() ||
        isLinkOrReparsePoint(after) ||
        after.size() != entry.size ||
        after.lastModified().toMSecsSinceEpoch() !=
            entry.lastModifiedMilliseconds ||
        !staged.isFile() || staged.size() != entry.size)
    {
        errorMessage = QStringLiteral(
            "Source file changed while it was being copied: %1")
                .arg(entry.sourcePath);
        return false;
    }
    hash = QString::fromLatin1(hasher.result().toHex());
    return true;
}

QJsonObject reportEntryJson(
    const AndroidExternalResourceExportEntry& entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("action"), actionName(entry.action));
    object.insert(QStringLiteral("sourcePath"), entry.sourcePath);
    object.insert(QStringLiteral("outputPath"), entry.outputPath);
    object.insert(QStringLiteral("directory"), entry.directory);
    object.insert(QStringLiteral("size"),
        QString::number(entry.size));
    if (!entry.sha256.isEmpty())
        object.insert(QStringLiteral("sha256"), entry.sha256);
    if (!entry.reason.isEmpty())
        object.insert(QStringLiteral("reason"), entry.reason);
    return object;
}

bool writeBytesAtomically(
    const QString& path,
    const QByteArray& bytes,
    QString& errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        errorMessage = QStringLiteral("Unable to create export report: %1 (%2)")
            .arg(path, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit())
    {
        errorMessage = QStringLiteral("Unable to publish export report: %1 (%2)")
            .arg(path, file.errorString());
        return false;
    }
    return true;
}

bool writeReports(
    const QString& bundleRoot,
    const AndroidExternalResourceExportReport& report,
    QString& errorMessage)
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), report.schemaVersion);
    root.insert(QStringLiteral("status"), QStringLiteral("completed"));
    root.insert(QStringLiteral("createdAtUtc"),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("resourcePackId"), report.resourcePackId);
    root.insert(QStringLiteral("resourcePackName"), report.resourcePackName);
    root.insert(QStringLiteral("resourcePackEntryKey"),
        report.resourcePackEntryKey);
    root.insert(QStringLiteral("sourceCollectionRoot"),
        report.sourceCollectionRoot);
    root.insert(QStringLiteral("sourcePackRoot"), report.sourcePackRoot);
    root.insert(QStringLiteral("bundleDirectory"), report.bundleDirectory);
    root.insert(QStringLiteral("installedManifestPath"),
        report.installedManifestPath);
    if (!report.retainedBackupPath.isEmpty())
    {
        root.insert(QStringLiteral("retainedBackupPath"),
            report.retainedBackupPath);
    }
    root.insert(QStringLiteral("copiedCount"), report.copiedCount());
    root.insert(QStringLiteral("skippedCount"), report.skippedCount());
    root.insert(QStringLiteral("failedCount"), report.failedCount());

    QJsonArray warnings;
    for (const QString& warning : report.warnings)
        warnings.append(warning);
    root.insert(QStringLiteral("warnings"), warnings);

    QJsonArray entries;
    for (const AndroidExternalResourceExportEntry& entry : report.entries)
        entries.append(reportEntryJson(entry));
    root.insert(QStringLiteral("entries"), entries);

    const QByteArray json = QJsonDocument(root).toJson(
        QJsonDocument::Indented);
    if (!writeBytesAtomically(
            QDir(bundleRoot).filePath(
                QStringLiteral("android_external_resource_report.json")),
            json,
            errorMessage))
    {
        return false;
    }

    QString text;
    text += QStringLiteral("Android external resource export\n");
    text += QStringLiteral("Pack: %1 (%2)\n")
        .arg(report.resourcePackName, report.resourcePackId);
    text += QStringLiteral("Source: %1\n")
        .arg(report.sourcePackRoot);
    text += QStringLiteral("Bundle: %1\n")
        .arg(report.bundleDirectory);
    text += QStringLiteral("Device manifest: %1\n")
        .arg(report.installedManifestPath);
    text += QStringLiteral("Copied: %1, skipped: %2, failed: %3\n")
        .arg(report.copiedCount())
        .arg(report.skippedCount())
        .arg(report.failedCount());
    if (!report.warnings.isEmpty())
    {
        text += QStringLiteral("\nWarnings:\n");
        for (const QString& warning : report.warnings)
            text += QStringLiteral("- %1\n").arg(warning);
    }
    text += QStringLiteral("\nFiles:\n");
    for (const AndroidExternalResourceExportEntry& entry : report.entries)
    {
        text += QStringLiteral("[%1] %2 -> %3")
            .arg(actionName(entry.action), entry.sourcePath,
                 entry.outputPath);
        if (!entry.sha256.isEmpty())
            text += QStringLiteral(" sha256=%1").arg(entry.sha256);
        if (!entry.reason.isEmpty())
            text += QStringLiteral(" reason=%1").arg(entry.reason);
        text += '\n';
    }
    if (!writeBytesAtomically(
            QDir(bundleRoot).filePath(
                QStringLiteral("android_external_resource_report.txt")),
            text.toUtf8(),
            errorMessage))
    {
        return false;
    }

    const QString installText = QStringLiteral(
        "JXQY Android external resource bundle\n\n"
        "Copy the Download directory in this bundle to the root of the "
        "Android shared storage.\n\n"
        "The resource pack manifest must then exist at:\n%1\n\n"
        "The game scans direct child directories under:\n"
        "/storage/emulated/0/Download/jxqy/assets/\n"
        "No resources.ini file is required for this external package.\n")
            .arg(report.installedManifestPath);
    return writeBytesAtomically(
        QDir(bundleRoot).filePath(QStringLiteral("INSTALL.txt")),
        installText.toUtf8(),
        errorMessage);
}

void updateProgress(
    const AndroidExternalResourceExportOptions& options,
    int current,
    int total,
    const QString& path)
{
    if (options.progressCallback)
        options.progressCallback(current, total, path);
}
}

int AndroidExternalResourceExportReport::copiedCount() const
{
    return static_cast<int>(std::count_if(
        entries.cbegin(), entries.cend(),
        [](const AndroidExternalResourceExportEntry& entry)
        {
            return entry.action ==
                AndroidExternalResourceExportAction::Copy;
        }));
}

int AndroidExternalResourceExportReport::skippedCount() const
{
    return static_cast<int>(std::count_if(
        entries.cbegin(), entries.cend(),
        [](const AndroidExternalResourceExportEntry& entry)
        {
            return entry.action ==
                AndroidExternalResourceExportAction::Skip;
        }));
}

int AndroidExternalResourceExportReport::failedCount() const
{
    return static_cast<int>(std::count_if(
        entries.cbegin(), entries.cend(),
        [](const AndroidExternalResourceExportEntry& entry)
        {
            return entry.action ==
                AndroidExternalResourceExportAction::Fail;
        }));
}

AndroidExternalResourceExportResult
AndroidExternalResourcePackager::exportBundle(
    const AndroidExternalResourceExportOptions& options)
{
    AndroidExternalResourceExportResult result;
    AndroidExternalResourceExportReport& report = result.report;
    report.resourcePackId = options.activePack.profile.id.trimmed();
    report.resourcePackName = options.activePack.profile.name.trimmed();
    report.resourcePackEntryKey = options.activePack.stableEntryKey;
    report.sourceCollectionRoot =
        EditorAssetPath::normalizedAbsolutePath(options.collectionRoot);
    report.sourcePackRoot =
        EditorAssetPath::normalizedAbsolutePath(
            options.activePack.rootPath);
    report.bundleDirectory =
        EditorAssetPath::normalizedAbsolutePath(options.bundleDirectory);
    auto fail = [&](const QString& message)
    {
        result.status = cancellationRequested(options)
            ? AndroidExternalResourceExportStatus::Cancelled
            : AndroidExternalResourceExportStatus::Failed;
        result.errorMessage = message;
        return result;
    };

    if (!options.bundleDirectory.trimmed().isEmpty() &&
        (AuthoringMutationGate::wouldReplaceResourceCollection(
             report.bundleDirectory) ||
         AuthoringMutationGate::isInsideResource(
             report.bundleDirectory)))
    {
        return fail(QStringLiteral(
            "The Android bundle output cannot replace or be placed inside a resource."));
    }

    if (options.collectionRoot.trimmed().isEmpty() ||
        !QFileInfo(report.sourceCollectionRoot).isDir())
    {
        return fail(QStringLiteral(
            "The resource collection directory is unavailable."));
    }
    if (!options.activePack.profile.isValid() ||
        !QFileInfo(report.sourcePackRoot).isDir() ||
        !EditorAssetPath::isInside(
            report.sourceCollectionRoot, report.sourcePackRoot))
    {
        return fail(QStringLiteral(
            "The selected resource pack is not a valid pack inside the resource collection."));
    }
    if (!QFileInfo(options.activePack.manifestPath).isFile())
    {
        return fail(QStringLiteral(
            "The selected resource pack has no root game_profile.ini."));
    }
    const QString packDirectoryName =
        QFileInfo(report.sourcePackRoot).fileName();
    QString safePackDirectoryName;
    if (!isSafeRelativeOutputPath(
            packDirectoryName, safePackDirectoryName) ||
        safePackDirectoryName.contains('/'))
    {
        return fail(QStringLiteral(
            "The resource pack directory name cannot be represented in the Android assets directory."));
    }
    report.installedManifestPath = QStringLiteral(
        "/storage/emulated/0/Download/jxqy/assets/%1/game_profile.ini")
            .arg(safePackDirectoryName);

    if (options.bundleDirectory.trimmed().isEmpty())
        return fail(QStringLiteral("No bundle output directory was selected."));
    const QFileInfo bundleInfo(report.bundleDirectory);
    const QString bundleParent = bundleInfo.dir().absolutePath();
    if (bundleInfo.fileName().isEmpty() ||
        !QFileInfo(bundleParent).isDir())
    {
        return fail(QStringLiteral(
            "The parent directory of the bundle output is unavailable."));
    }
    if (pathsOverlapLexically(
            report.sourceCollectionRoot,
            report.bundleDirectory) ||
        pathsOverlapAfterCanonicalResolution(
            report.sourceCollectionRoot,
            report.bundleDirectory))
    {
        return fail(QStringLiteral(
            "The bundle output directory and resource collection must be separate directories."));
    }
    if ((bundleInfo.exists() || bundleInfo.isSymLink()) &&
        !bundleInfo.isDir())
    {
        return fail(QStringLiteral(
            "The bundle output path is not a directory."));
    }

    QString outputParentLinkPath;
    if (existingParentChainContainsLinkOrReparsePoint(
            bundleParent,
            outputParentLinkPath))
    {
        return fail(QStringLiteral(
            "The bundle output parent chain contains a link or reparse point: %1")
                .arg(outputParentLinkPath));
    }
    if (bundleInfo.exists() || bundleInfo.isSymLink())
    {
        QString offendingPath;
        if (treeContainsLinkOrReparsePoint(
                report.bundleDirectory, offendingPath))
        {
            return fail(QStringLiteral(
                "The existing bundle contains a link or reparse point and cannot be replaced safely: %1")
                    .arg(offendingPath));
        }
    }

    QStringList recoveryErrors;
    const auto readLock =
        DurableFileTransaction::acquireRecoveredReadLock(
            report.sourceCollectionRoot, recoveryErrors);
    if (!readLock)
    {
        return fail(QStringLiteral(
            "Unable to obtain a coherent resource collection snapshot: %1")
                .arg(recoveryErrors.join(QStringLiteral(" | "))));
    }
    if (!readLock->addResourcePath(report.sourcePackRoot) ||
        !readLock->addResourcePath(report.bundleDirectory))
    {
        return fail(QStringLiteral(
            "The resource is being read or written by another editor operation."));
    }
    GameProfile lockedProfile;
    const QString lockedManifestPath = QDir(report.sourcePackRoot).filePath(
        QStringLiteral("game_profile.ini"));
    if (!lockedProfile.loadFromFile(lockedManifestPath) ||
        !lockedProfile.isValid() ||
        lockedProfile.id.trimmed().compare(
            report.resourcePackId, Qt::CaseSensitive) != 0)
    {
        return fail(QStringLiteral(
            "The selected resource manifest changed before the export snapshot was locked."));
    }
    report.resourcePackName = lockedProfile.name.trimmed();
    if (cancellationRequested(options))
        return fail(QStringLiteral("cancelled"));

    QString errorMessage;
    QString sourceLinkPath;
    if (pathContainsLinkOrReparsePoint(
            report.sourceCollectionRoot,
            report.sourcePackRoot,
            sourceLinkPath))
    {
        return fail(QStringLiteral(
            "The selected resource pack crosses a link or reparse point: %1")
                .arg(sourceLinkPath));
    }

    QList<CopyRoot> copyRoots;
    copyRoots.append({report.sourcePackRoot, safePackDirectoryName});
    const auto catalog = RuntimeResource::loadResourceCatalogSnapshot(
        std::filesystem::u8path(
            report.sourceCollectionRoot.toUtf8().constData()));
    const QString commonRoot = catalog.succeeded()
        ? EditorAssetPath::normalizedAbsolutePath(
            QString::fromStdString(
                catalog.snapshot.commonResourceRoot.u8string()))
        : QString();
    const QString commonRelativePath = commonRoot.isEmpty()
        ? QString()
        : QDir(report.sourceCollectionRoot).relativeFilePath(commonRoot);
    if (!addOptionalCopyRoot(
        QStringLiteral("Collection CommonPath"),
        commonRelativePath,
        true,
        report.sourceCollectionRoot,
        report.sourceCollectionRoot,
        QString(),
        copyRoots,
        report,
        errorMessage))
    {
        return fail(errorMessage);
    }
    if (!lockedProfile.dependencyId.trimmed().isEmpty())
    {
        appendWarningOnce(
            report,
            QStringLiteral(
                "DependencyId declarations are preserved but dependency packs are not copied: %1")
                .arg(lockedProfile.dependencyId.trimmed()));
    }

    for (const CopyRoot& copyRoot : copyRoots)
    {
        if (!readLock->addResourcePath(copyRoot.sourceRoot))
        {
            return fail(QStringLiteral(
                "A copied resource package is being updated by another operation: %1")
                    .arg(copyRoot.sourceRoot));
        }
    }

    QList<PlannedEntry> plan;
    QHash<QString, int> outputOwners;
    for (const CopyRoot& copyRoot : copyRoots)
    {
        if (cancellationRequested(options))
            return fail(QStringLiteral("cancelled"));
        if (!scanCopyRoot(
                copyRoot,
                plan,
                outputOwners,
                report,
                errorMessage))
        {
            return fail(errorMessage);
        }
    }

    const QString stagingPath = uniqueSiblingPath(
        report.bundleDirectory,
        QStringLiteral("android-export-staging"));
    if (stagingPath.isEmpty() || !QDir().mkpath(stagingPath))
    {
        return fail(QStringLiteral(
            "Unable to create a same-volume staging directory."));
    }
    const auto discardStaging = [&]()
    {
        removeGeneratedTree(stagingPath);
    };

    const QString stagedAssetsRoot = QDir(stagingPath).filePath(
        QStringLiteral("Download/jxqy/assets"));
    if (!QDir().mkpath(stagedAssetsRoot))
    {
        discardStaging();
        return fail(QStringLiteral(
            "Unable to create the Android external assets directory."));
    }

    int current = 0;
    const int total = static_cast<int>(std::count_if(
        plan.cbegin(), plan.cend(),
        [](const PlannedEntry& entry)
        {
            return !entry.skipped;
        }));
    for (const PlannedEntry& entry : plan)
    {
        if (entry.skipped)
            continue;
        if (cancellationRequested(options))
        {
            discardStaging();
            return fail(QStringLiteral("cancelled"));
        }
        const QString targetPath = QDir(stagedAssetsRoot).filePath(
            entry.outputRelativePath);
        if (entry.directory)
        {
            if (!QDir().mkpath(targetPath))
            {
                markEntryFailed(
                    report,
                    entry.reportEntryIndex,
                    QStringLiteral("create-directory-failed"));
                discardStaging();
                return fail(QStringLiteral(
                    "Unable to create staged directory: %1")
                        .arg(targetPath));
            }
        }
        else
        {
            QString hash;
            if (!copyFileWithHash(
                    entry,
                    targetPath,
                    options,
                    hash,
                    errorMessage))
            {
                markEntryFailed(
                    report,
                    entry.reportEntryIndex,
                    cancellationRequested(options)
                        ? QStringLiteral("cancelled")
                        : QStringLiteral("copy-failed"));
                discardStaging();
                return fail(errorMessage);
            }
            report.entries[entry.reportEntryIndex].sha256 = hash;
        }
        ++current;
        updateProgress(
            options, current, total, entry.sourcePath);
    }

    if (cancellationRequested(options))
    {
        discardStaging();
        return fail(QStringLiteral("cancelled"));
    }
    if (!writeReports(stagingPath, report, errorMessage))
    {
        discardStaging();
        return fail(errorMessage);
    }
    if (faultRequested(FaultPoint::AfterStaging))
    {
        discardStaging();
        return fail(QStringLiteral(
            "Injected failure after staging."));
    }

    QString backupPath;
    if (QFileInfo::exists(report.bundleDirectory))
    {
        backupPath = uniqueSiblingPath(
            report.bundleDirectory,
            QStringLiteral("android-export-backup"));
        if (backupPath.isEmpty() ||
            !renameSibling(report.bundleDirectory, backupPath))
        {
            discardStaging();
            return fail(QStringLiteral(
                "Unable to move the previous bundle to a same-volume backup."));
        }
    }

    auto restorePreviousBundle = [&]()
    {
        bool restored = true;
        if (QFileInfo::exists(report.bundleDirectory))
            restored = removeGeneratedTree(report.bundleDirectory);
        if (!backupPath.isEmpty() && QFileInfo::exists(backupPath))
        {
            restored = renameSibling(
                backupPath, report.bundleDirectory) && restored;
        }
        return restored;
    };

    auto rememberRetainedBackup = [&]()
    {
        if (!backupPath.isEmpty() && QFileInfo::exists(backupPath))
            report.retainedBackupPath = backupPath;
    };

    const bool injectedAfterBackup =
        faultRequested(FaultPoint::AfterBackup);
    if (injectedAfterBackup || cancellationRequested(options))
    {
        const bool restored = restorePreviousBundle();
        if (!restored)
            rememberRetainedBackup();
        discardStaging();
        return fail(restored
            ? (injectedAfterBackup
                ? QStringLiteral("Injected failure after backup.")
                : QStringLiteral("cancelled-before-publication"))
            : QStringLiteral(
                "Export was cancelled, and the previous bundle could not be restored from %1")
                    .arg(backupPath));
    }
    if (!renameSibling(stagingPath, report.bundleDirectory))
    {
        const bool restored = restorePreviousBundle();
        if (!restored)
            rememberRetainedBackup();
        discardStaging();
        return fail(restored
            ? QStringLiteral(
                "Unable to publish the staged Android resource bundle.")
            : QStringLiteral(
                "Unable to publish the staged bundle or restore the previous bundle from %1")
                    .arg(backupPath));
    }
    if (faultRequested(FaultPoint::AfterPublish))
    {
        const bool restored = restorePreviousBundle();
        if (!restored)
            rememberRetainedBackup();
        return fail(restored
            ? QStringLiteral("Injected failure after publication.")
            : QStringLiteral(
                "Injected publication failure; the previous bundle remains at %1")
                    .arg(backupPath));
    }

    if (!backupPath.isEmpty() && !removeGeneratedTree(backupPath))
    {
        report.retainedBackupPath = backupPath;
        appendWarningOnce(
            report,
            QStringLiteral("The new bundle was published, but the previous bundle backup was retained: %1")
                .arg(backupPath));
        QString reportWarningError;
        if (!writeReports(
                report.bundleDirectory,
                report,
                reportWarningError))
        {
            appendWarningOnce(
                report,
                QStringLiteral("The retained-backup warning could not be written to the published report: %1")
                    .arg(reportWarningError));
        }
    }

    result.status = AndroidExternalResourceExportStatus::Success;
    return result;
}

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
void AndroidExternalResourcePackager::setFaultInjectorForTests(
    FaultInjector injector)
{
    QMutexLocker locker(&faultInjectorMutex);
    faultInjector = std::move(injector);
}
#endif
