#include "DurableFileTransaction.h"
#include "AuthoringMutationGate.h"

#include "EditorAssetPath.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <mutex>
#include <utility>
#include <vector>

namespace
{
constexpr int ManifestVersion = 1;
constexpr auto ManifestName = "manifest.json";
constexpr auto CommitMarkerName = "committed";

struct TransactionEntry
{
    QString targetPath;
    QString targetRelativePath;
    QString stagedRelativePath;
    QString backupRelativePath;
    QByteArray newDigest;
    QByteArray oldDigest;
    bool removesTarget = false;
    bool hadOriginal = false;
    bool checksExpectedOriginal = false;
    bool expectedOriginalExists = false;
    QByteArray expectedOldDigest;
};

bool removeDirectory(const QString& path);

bool cleanupCommittedTransaction(const QString& transactionPath,
                                 const QList<TransactionEntry>& entries,
                                 QStringList& errors)
{
    bool ok = true;
    for (const TransactionEntry& entry : entries)
    {
        const QStringList relativeArtifacts = {
            entry.stagedRelativePath, entry.backupRelativePath};
        for (const QString& relativeArtifact : relativeArtifacts)
        {
            if (relativeArtifact.isEmpty())
                continue;
            const QString artifactPath = EditorAssetPath::normalizedAbsolutePath(
                QDir(transactionPath).filePath(relativeArtifact));
            if (!EditorAssetPath::isInside(transactionPath, artifactPath))
            {
                errors << QCoreApplication::translate(
                    "DurableFileTransaction",
                    "事务清理路径越界: %1").arg(artifactPath);
                ok = false;
            }
            else if (QFileInfo::exists(artifactPath) && !QFile::remove(artifactPath))
            {
                errors << QCoreApplication::translate(
                    "DurableFileTransaction",
                    "无法清理已提交事务文件: %1").arg(artifactPath);
                ok = false;
            }
        }
    }
    if (!ok)
        return false;

    // Keep the commit marker until all rollback material is gone. If cleanup
    // is interrupted, startup recovery must continue to recognize the new
    // generation instead of mistaking it for an uncommitted transaction.
    const QString manifestPath = QDir(transactionPath).filePath(ManifestName);
    if (QFileInfo::exists(manifestPath) && !QFile::remove(manifestPath))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "无法清理已提交事务清单: %1").arg(manifestPath);
        return false;
    }
    const QString markerPath = QDir(transactionPath).filePath(CommitMarkerName);
    if (QFileInfo::exists(markerPath) && !QFile::remove(markerPath))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "无法清理事务提交标记: %1").arg(markerPath);
        return false;
    }
    if (!removeDirectory(transactionPath))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "无法清理已提交事务目录: %1").arg(transactionPath);
        return false;
    }
    return true;
}

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
DurableFileTransaction::FaultInjector& faultInjector()
{
    static DurableFileTransaction::FaultInjector injector;
    return injector;
}

std::mutex& faultInjectorMutex()
{
    static std::mutex mutex;
    return mutex;
}

DurableFileTransaction::FaultInjector currentFaultInjector()
{
    std::lock_guard<std::mutex> lock(faultInjectorMutex());
    return faultInjector();
}
#endif

QString normalizedRoot(const QString& rootPath)
{
    if (rootPath.trimmed().isEmpty())
        return {};
    return EditorAssetPath::normalizedAbsolutePath(rootPath);
}

QString digestFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

bool removeDirectory(const QString& path)
{
    if (!QFileInfo::exists(path))
        return true;
    return QDir(path).removeRecursively();
}

void removeEmptyTransactionStore(const QString& rootPath)
{
    const QString storePath = DurableFileTransaction::transactionStorePath(rootPath);
    QDir store(storePath);
    if (store.exists() &&
        store.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
    {
        QDir().rmdir(storePath);
    }
}

bool writeAtomically(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
    {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool relativePathIsSafe(const QString& path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path))
        return false;
    const QString clean = QDir::cleanPath(path);
    return clean != ".." && !clean.startsWith("../") &&
        !clean.startsWith("..\\") && clean != ".";
}

bool loadManifest(const QString& rootPath,
                  const QString& transactionPath,
                  QList<TransactionEntry>& entries,
                  QString& errorMessage)
{
    QFile file(QDir(transactionPath).filePath(ManifestName));
    if (!file.open(QIODevice::ReadOnly))
    {
        errorMessage = QCoreApplication::translate(
            "DurableFileTransaction",
            "无法读取事务清单: %1").arg(file.fileName());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        errorMessage = QCoreApplication::translate(
            "DurableFileTransaction",
            "事务清单格式无效: %1").arg(file.fileName());
        return false;
    }

    const QJsonObject rootObject = document.object();
    if (rootObject.value("version").toInt() != ManifestVersion ||
        !rootObject.value("entries").isArray())
    {
        errorMessage = QCoreApplication::translate(
            "DurableFileTransaction",
            "事务清单版本或条目无效: %1").arg(file.fileName());
        return false;
    }

    QSet<QString> targetKeys;
    const QJsonArray jsonEntries = rootObject.value("entries").toArray();
    if (jsonEntries.isEmpty())
    {
        errorMessage = QCoreApplication::translate(
            "DurableFileTransaction",
            "事务清单没有文件条目: %1").arg(file.fileName());
        return false;
    }

    for (const QJsonValue& value : jsonEntries)
    {
        if (!value.isObject())
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "事务清单包含非对象条目: %1").arg(file.fileName());
            return false;
        }
        const QJsonObject object = value.toObject();
        TransactionEntry entry;
        entry.targetRelativePath = object.value("target").toString();
        entry.stagedRelativePath = object.value("staged").toString();
        entry.backupRelativePath = object.value("backup").toString();
        entry.newDigest = object.value("newSha256").toString().toLatin1();
        entry.oldDigest = object.value("oldSha256").toString().toLatin1();
        const QString operation = object.value("operation").toString();
        if (operation != "write" && operation != "remove")
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "事务清单包含未知操作: %1").arg(operation);
            return false;
        }
        entry.removesTarget = operation == "remove";
        entry.hadOriginal = object.value("hadOriginal").toBool();

        const int entryIndex = entries.size();
        const QString expectedStage = QStringLiteral("staged/%1.new")
            .arg(entryIndex, 4, 10, QChar('0'));
        const QString expectedBackup = QStringLiteral("backup/%1.old")
            .arg(entryIndex, 4, 10, QChar('0'));
        const QRegularExpression digestPattern(QStringLiteral("^[0-9a-f]{64}$"));
        const bool digestsValid =
            (entry.removesTarget
                ? entry.newDigest.isEmpty()
                : digestPattern.match(QString::fromLatin1(entry.newDigest)).hasMatch()) &&
            (entry.hadOriginal
                ? digestPattern.match(QString::fromLatin1(entry.oldDigest)).hasMatch()
                : entry.oldDigest.isEmpty());

        if (!relativePathIsSafe(entry.targetRelativePath) ||
            !relativePathIsSafe(entry.backupRelativePath) ||
            entry.backupRelativePath != expectedBackup ||
            (entry.removesTarget
                ? !entry.stagedRelativePath.isEmpty()
                : entry.stagedRelativePath != expectedStage ||
                  !relativePathIsSafe(entry.stagedRelativePath)) ||
            !digestsValid)
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "事务清单包含越界路径: %1").arg(file.fileName());
            return false;
        }

        entry.targetPath = EditorAssetPath::normalizedAbsolutePath(
            QDir(rootPath).filePath(entry.targetRelativePath));
        const QString targetKey = EditorAssetPath::comparisonKey(entry.targetPath);
        if (!EditorAssetPath::isInside(rootPath, entry.targetPath) ||
            targetKeys.contains(targetKey))
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "事务清单包含重复或越界目标: %1")
                .arg(entry.targetPath);
            return false;
        }
        targetKeys.insert(targetKey);
        entries.append(entry);
    }
    return true;
}

bool verifyCommittedGeneration(const QString& rootPath,
                               const QList<TransactionEntry>& entries,
                               QStringList& errors)
{
    bool ok = true;
    for (const TransactionEntry& entry : entries)
    {
        if (entry.removesTarget)
        {
            if (QFileInfo::exists(entry.targetPath))
            {
                errors << QCoreApplication::translate(
                    "DurableFileTransaction",
                    "已提交事务的删除目标重新出现: %1")
                    .arg(entry.targetPath);
                ok = false;
            }
            continue;
        }

        const QString digest = digestFile(entry.targetPath);
        if (digest.isEmpty() || digest.toLatin1() != entry.newDigest)
        {
            errors << QCoreApplication::translate(
                "DurableFileTransaction",
                "已提交事务的新代文件缺失或校验失败: %1")
                .arg(entry.targetPath);
            ok = false;
        }
    }
    Q_UNUSED(rootPath);
    return ok;
}

bool restoreOldGeneration(const QString& rootPath,
                          const QString& transactionPath,
                          const QList<TransactionEntry>& entries,
                          QStringList& errors)
{
    bool ok = true;
    for (auto iterator = entries.crbegin(); iterator != entries.crend(); ++iterator)
    {
        const TransactionEntry& entry = *iterator;
        const QString backupPath = EditorAssetPath::normalizedAbsolutePath(
            QDir(transactionPath).filePath(entry.backupRelativePath));
        if (!EditorAssetPath::isInside(transactionPath, backupPath))
        {
            errors << QCoreApplication::translate(
                "DurableFileTransaction",
                "事务备份路径越界: %1").arg(backupPath);
            ok = false;
            continue;
        }

        if (entry.hadOriginal)
        {
            if (QFileInfo::exists(backupPath))
            {
                if (QFileInfo::exists(entry.targetPath) && !QFile::remove(entry.targetPath))
                {
                    errors << QCoreApplication::translate(
                        "DurableFileTransaction",
                        "无法移除未完成的新代文件: %1")
                        .arg(entry.targetPath);
                    ok = false;
                    continue;
                }
                if (!QDir().mkpath(QFileInfo(entry.targetPath).absolutePath()) ||
                    !QFile::rename(backupPath, entry.targetPath))
                {
                    errors << QCoreApplication::translate(
                        "DurableFileTransaction",
                        "无法恢复旧代文件: %1（备份在 %2）")
                        .arg(entry.targetPath, backupPath);
                    ok = false;
                    continue;
                }
            }

            const QString restoredDigest = digestFile(entry.targetPath);
            if (restoredDigest.isEmpty() || restoredDigest.toLatin1() != entry.oldDigest)
            {
                errors << QCoreApplication::translate(
                    "DurableFileTransaction",
                    "旧代文件恢复后校验失败: %1")
                    .arg(entry.targetPath);
                ok = false;
            }
        }
        else if (QFileInfo::exists(entry.targetPath))
        {
            const QString currentDigest = digestFile(entry.targetPath);
            if (!entry.removesTarget && currentDigest.toLatin1() != entry.newDigest)
            {
                errors << QCoreApplication::translate(
                    "DurableFileTransaction",
                    "未完成事务的新增目标已被外部修改，未自动删除: %1")
                    .arg(entry.targetPath);
                ok = false;
            }
            else if (!QFile::remove(entry.targetPath))
            {
                errors << QCoreApplication::translate(
                    "DurableFileTransaction",
                    "无法移除未完成事务的新增目标: %1")
                    .arg(entry.targetPath);
                ok = false;
            }
        }
    }
    Q_UNUSED(rootPath);
    return ok;
}

bool recoverTransaction(const QString& rootPath,
                        const QString& transactionPath,
                        QStringList& errors)
{
    const QString manifestPath = QDir(transactionPath).filePath(ManifestName);
    if (!QFileInfo::exists(manifestPath))
    {
        if (!removeDirectory(transactionPath))
            errors << QCoreApplication::translate(
                "DurableFileTransaction",
                "无法清理未发布的事务暂存目录: %1")
                .arg(transactionPath);
        return !QFileInfo::exists(transactionPath);
    }

    QList<TransactionEntry> entries;
    QString loadError;
    if (!loadManifest(rootPath, transactionPath, entries, loadError))
    {
        errors << loadError;
        return false;
    }

    bool recovered = false;
    if (QFileInfo::exists(QDir(transactionPath).filePath(CommitMarkerName)))
        recovered = verifyCommittedGeneration(rootPath, entries, errors);
    else
        recovered = restoreOldGeneration(rootPath, transactionPath, entries, errors);

    const bool committed = QFileInfo::exists(
        QDir(transactionPath).filePath(CommitMarkerName));
    if (recovered && committed)
    {
        recovered = cleanupCommittedTransaction(transactionPath, entries, errors);
    }
    else if (recovered && !removeDirectory(transactionPath))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "文件代已恢复，但事务目录无法清理: %1")
            .arg(transactionPath);
        recovered = false;
    }
    return recovered;
}

QString transactionLockPath(const QString& rootPath)
{
    QString lockRoot = QFileInfo(rootPath).canonicalFilePath();
    if (lockRoot.isEmpty())
    {
        lockRoot = EditorAssetPath::normalizedAbsolutePath(rootPath);
    }
    const QByteArray rootKey =
        EditorAssetPath::comparisonKey(lockRoot).toUtf8();
    const QString lockName = QString::fromLatin1(
        QCryptographicHash::hash(
            rootKey, QCryptographicHash::Sha256).toHex());
    QString temporaryRoot =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (temporaryRoot.isEmpty())
    {
        temporaryRoot = QDir::tempPath();
    }
    return QDir(temporaryRoot).filePath(
        QStringLiteral("jxqy-editor-file-transaction-locks/%1.lock")
            .arg(lockName));
}

bool recoverPendingUnlocked(const QString& root,
                            QStringList& errors)
{
    const QString storePath = DurableFileTransaction::transactionStorePath(root);
    if (!EditorAssetPath::isInside(root, storePath))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "事务目录越过资源根: %1").arg(storePath);
        return false;
    }
    if (!QFileInfo::exists(storePath))
        return true;

    QDir store(storePath);
    const QStringList transactionIds = store.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    bool ok = true;
    for (const QString& transactionId : transactionIds)
    {
        const QString transactionPath = store.filePath(transactionId);
        if (!EditorAssetPath::isInside(root, transactionPath))
        {
            errors << QCoreApplication::translate(
                "DurableFileTransaction",
                "事务子目录越过资源根: %1")
                .arg(transactionPath);
            ok = false;
            continue;
        }
        QStringList transactionErrors;
        if (!recoverTransaction(root, transactionPath, transactionErrors))
            ok = false;
        errors.append(transactionErrors);
    }

    const QStringList unexpectedFiles = store.entryList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    if (!unexpectedFiles.isEmpty())
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "事务目录包含未知文件: %1")
            .arg(unexpectedFiles.join(QStringLiteral(", ")));
        ok = false;
    }
    if (ok && store.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
        QDir().rmdir(storePath);
    return ok;
}

bool addPendingTransactionTargetLocks(
    const QString& root,
    AuthoringMutationGate::Lease& lease,
    QStringList& errors)
{
    const QString storePath =
        DurableFileTransaction::transactionStorePath(root);
    if (!QFileInfo::exists(storePath))
        return true;
    const QDir store(storePath);
    const QStringList transactionIds = store.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QSet<QString> lockedTargets;
    for (const QString& transactionId : transactionIds)
    {
        const QString transactionPath = store.filePath(transactionId);
        const QString manifestPath =
            QDir(transactionPath).filePath(ManifestName);
        if (!QFileInfo::exists(manifestPath))
            continue;
        QList<TransactionEntry> entries;
        QString loadError;
        if (!loadManifest(root, transactionPath, entries, loadError))
        {
            errors << loadError;
            return false;
        }
        for (const TransactionEntry& entry : entries)
        {
            const QString targetKey =
                EditorAssetPath::comparisonKey(entry.targetPath);
            if (lockedTargets.contains(targetKey))
                continue;
            if (!lease.addResourcePath(entry.targetPath))
            {
                errors << QCoreApplication::translate(
                    "DurableFileTransaction",
                    "在线资源正在更新，无法恢复事务目标: %1")
                    .arg(entry.targetPath);
                return false;
            }
            lockedTargets.insert(targetKey);
        }
    }
    return true;
}

}

struct DurableFileRecoveredReadLock::State
{
    AuthoringMutationGate::Lease authoringGateLease;
    std::vector<std::unique_ptr<QLockFile>> rootLocks;
    QSet<QString> recoveredRootKeys;
};

DurableFileRecoveredReadLock::DurableFileRecoveredReadLock(
    std::unique_ptr<State> value)
    : state(std::move(value))
{
}

DurableFileRecoveredReadLock::~DurableFileRecoveredReadLock() =
    default;

bool DurableFileRecoveredReadLock::addResourcePath(
    const QString& path)
{
    return state != nullptr &&
        state->authoringGateLease.addResourcePath(path);
}

bool DurableFileRecoveredReadLock::addRecoveredReadRoot(
    const QString& rootPath, QStringList& errors)
{
    if (state == nullptr)
        return false;
    const QString root = normalizedRoot(rootPath);
    if (root.isEmpty() || !QFileInfo(root).isDir())
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "事务资源根无效: %1").arg(rootPath);
        return false;
    }
    const QString rootKey = EditorAssetPath::comparisonKey(root);
    if (rootKey.isEmpty())
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "无法规范化事务资源根: %1").arg(rootPath);
        return false;
    }
    if (state->recoveredRootKeys.contains(rootKey))
        return true;
    if (!state->authoringGateLease.addResourcePath(root))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "在线资源正在更新，无法恢复并锁定资源目录: %1")
            .arg(root);
        return false;
    }

    const QString lockPath = transactionLockPath(root);
    if (!QDir().mkpath(QFileInfo(lockPath).absolutePath()))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "无法创建事务锁目录: %1")
            .arg(QFileInfo(lockPath).absolutePath());
        return false;
    }
    auto rootLock = std::make_unique<QLockFile>(lockPath);
    if (!rootLock->tryLock(1000))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "另一个编辑器正在保存或恢复该资源目录: %1")
            .arg(root);
        return false;
    }
    if (!addPendingTransactionTargetLocks(
            root, state->authoringGateLease, errors) ||
        !recoverPendingUnlocked(root, errors))
    {
        return false;
    }
    state->recoveredRootKeys.insert(rootKey);
    state->rootLocks.push_back(std::move(rootLock));
    return true;
}

struct DurableFileTransaction::State
{
    QString rootPath;
    QString transactionId;
    QString transactionPath;
    QList<TransactionEntry> entries;
    QSet<QString> targetKeys;
    std::unique_ptr<QLockFile> lock;
    AuthoringMutationGate::Lease mutationLease;
    bool storePrepared = false;
    bool preserveForRecovery = false;
    bool finished = false;

    explicit State(const QString& root)
        : rootPath(normalizedRoot(root))
        , transactionId(QUuid::createUuid().toString(QUuid::Id128))
        , transactionPath(QDir(DurableFileTransaction::transactionStorePath(rootPath))
              .filePath(transactionId))
    {
    }

    bool ensureStore(QString& errorMessage)
    {
        if (storePrepared)
            return true;
        AuthoringMutationGate::Lease requestedMutationLease =
            AuthoringMutationGate::instance().
                acquireMutationLeaseForPath(rootPath);
        if (!requestedMutationLease)
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "资源目录正在进行其他写入");
            return false;
        }
        if (rootPath.isEmpty() || !QFileInfo(rootPath).isDir())
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "事务资源根无效: %1").arg(rootPath);
            return false;
        }
        const QString lockPath = transactionLockPath(rootPath);
        if (!QDir().mkpath(QFileInfo(lockPath).absolutePath()))
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "无法创建事务锁目录: %1")
                .arg(QFileInfo(lockPath).absolutePath());
            return false;
        }
        lock = std::make_unique<QLockFile>(lockPath);
        if (!lock->tryLock(1000))
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "另一个编辑器正在保存或恢复该资源目录: %1")
                .arg(rootPath);
            lock.reset();
            return false;
        }

        QStringList recoveryErrors;
        if (!addPendingTransactionTargetLocks(
                rootPath, requestedMutationLease, recoveryErrors))
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "无法锁定待恢复事务的在线资源:\n%1")
                .arg(recoveryErrors.join('\n'));
            lock.reset();
            return false;
        }
        if (!recoverPendingUnlocked(rootPath, recoveryErrors))
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "存在无法恢复的旧事务:\n%1")
                .arg(recoveryErrors.join('\n'));
            lock.reset();
            return false;
        }

        if (!QDir().mkpath(QDir(transactionPath).filePath("staged")) ||
            !QDir().mkpath(QDir(transactionPath).filePath("backup")))
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "无法创建事务暂存目录: %1")
                .arg(transactionPath);
            removeDirectory(transactionPath);
            removeEmptyTransactionStore(rootPath);
            lock.reset();
            return false;
        }
        mutationLease =
            std::move(requestedMutationLease);
        storePrepared = true;
        return true;
    }

    bool prepareEntry(const QString& targetPath,
                      bool removesTarget,
                      TransactionEntry& entry,
                      QString& errorMessage)
    {
        if (!ensureStore(errorMessage))
            return false;
        entry.targetPath = EditorAssetPath::normalizedAbsolutePath(targetPath);
        if (!EditorAssetPath::isInside(rootPath, entry.targetPath))
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "事务目标越过资源根: %1").arg(targetPath);
            return false;
        }
        if (!mutationLease.addResourcePath(entry.targetPath))
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "在线资源正在更新，无法写入事务目标: %1")
                .arg(entry.targetPath);
            return false;
        }
        const QString key = EditorAssetPath::comparisonKey(entry.targetPath);
        if (targetKeys.contains(key))
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "事务包含重复目标: %1").arg(entry.targetPath);
            return false;
        }

        entry.targetRelativePath = QDir(rootPath).relativeFilePath(entry.targetPath);
        entry.targetRelativePath.replace('\\', '/');
        const int index = entries.size();
        entry.stagedRelativePath = QStringLiteral("staged/%1.new")
            .arg(index, 4, 10, QChar('0'));
        entry.backupRelativePath = QStringLiteral("backup/%1.old")
            .arg(index, 4, 10, QChar('0'));
        entry.removesTarget = removesTarget;
        targetKeys.insert(key);
        return true;
    }

    bool invokeFault(DurableFileTransaction::FaultPoint point,
                     int index,
                     QString& errorMessage)
    {
#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
        const DurableFileTransaction::FaultInjector injector =
            currentFaultInjector();
        if (!injector)
            return true;
        const DurableFileTransaction::FaultAction action =
            injector(point, index);
        if (action == DurableFileTransaction::FaultAction::Continue)
            return true;
        if (action == DurableFileTransaction::FaultAction::SimulateCrash)
        {
            preserveForRecovery = true;
            // The real process would release its OS lock while leaving the
            // journal and file artifacts behind. Mirror that state in tests.
            lock.reset();
            mutationLease.release();
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "测试故障注入模拟进程中断");
        }
        else
        {
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "测试故障注入模拟文件操作失败");
        }
        return false;
#else
        (void)point;
        (void)index;
        (void)errorMessage;
        return true;
#endif
    }
};

DurableFileTransaction::DurableFileTransaction(const QString& rootPath)
    : state(std::make_unique<State>(rootPath))
{
}

DurableFileTransaction::~DurableFileTransaction()
{
    if (state && !state->finished && !state->preserveForRecovery)
        cancel();
}

bool DurableFileTransaction::addPreparedWrite(const QString& targetPath,
                                              const QString& preparedPath,
                                              QString& errorMessage)
{
    TransactionEntry entry;
    if (!state->prepareEntry(targetPath, false, entry, errorMessage))
        return false;
    if (!state->mutationLease.addResourcePath(preparedPath))
    {
        errorMessage = QCoreApplication::translate(
            "DurableFileTransaction",
            "在线资源正在更新，无法搬运事务暂存源: %1")
            .arg(preparedPath);
        return false;
    }
    if (!QFileInfo(preparedPath).isFile())
    {
        errorMessage = QCoreApplication::translate(
            "DurableFileTransaction",
            "事务暂存源不存在: %1").arg(preparedPath);
        return false;
    }

    const QString stagedPath = QDir(state->transactionPath)
        .filePath(entry.stagedRelativePath);
    QFile::remove(stagedPath);
    if (!QFile::rename(preparedPath, stagedPath))
    {
        if (!QFile::copy(preparedPath, stagedPath) || !QFile::remove(preparedPath))
        {
            QFile::remove(stagedPath);
            errorMessage = QCoreApplication::translate(
                "DurableFileTransaction",
                "无法导入事务暂存文件: %1").arg(preparedPath);
            return false;
        }
    }
    entry.newDigest = digestFile(stagedPath).toLatin1();
    if (entry.newDigest.isEmpty())
    {
        errorMessage = QCoreApplication::translate(
            "DurableFileTransaction",
            "无法校验事务暂存文件: %1").arg(stagedPath);
        return false;
    }
    state->entries.append(entry);
    return true;
}

bool DurableFileTransaction::addBytesWrite(const QString& targetPath,
                                           const QByteArray& bytes,
                                           QString& errorMessage)
{
    TransactionEntry entry;
    if (!state->prepareEntry(targetPath, false, entry, errorMessage))
        return false;
    const QString stagedPath = QDir(state->transactionPath)
        .filePath(entry.stagedRelativePath);
    if (!writeAtomically(stagedPath, bytes))
    {
        errorMessage = QCoreApplication::translate(
            "DurableFileTransaction",
            "无法写入事务暂存文件: %1").arg(stagedPath);
        return false;
    }
    entry.newDigest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    state->entries.append(entry);
    return true;
}

bool DurableFileTransaction::addBytesWriteChecked(
    const QString& targetPath,
    const QByteArray& bytes,
    bool expectedTargetExists,
    const QByteArray& expectedTargetBytes,
    QString& errorMessage)
{
    TransactionEntry entry;
    if (!state->prepareEntry(targetPath, false, entry, errorMessage))
        return false;
    const QString stagedPath = QDir(state->transactionPath)
        .filePath(entry.stagedRelativePath);
    if (!writeAtomically(stagedPath, bytes))
    {
        errorMessage = QCoreApplication::translate(
            "DurableFileTransaction",
            "无法写入事务暂存文件: %1").arg(stagedPath);
        return false;
    }
    entry.newDigest =
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    entry.checksExpectedOriginal = true;
    entry.expectedOriginalExists = expectedTargetExists;
    if (expectedTargetExists)
    {
        entry.expectedOldDigest = QCryptographicHash::hash(
            expectedTargetBytes, QCryptographicHash::Sha256).toHex();
    }
    state->entries.append(entry);
    return true;
}

bool DurableFileTransaction::addRemoval(const QString& targetPath,
                                        QString& errorMessage)
{
    TransactionEntry entry;
    if (!state->prepareEntry(targetPath, true, entry, errorMessage))
        return false;
    state->entries.append(entry);
    return true;
}

bool DurableFileTransaction::commit(QString& errorOrWarning)
{
    errorOrWarning.clear();
    if (state->entries.isEmpty())
    {
        errorOrWarning = QCoreApplication::translate(
            "DurableFileTransaction",
            "事务没有文件条目");
        return false;
    }

    QJsonArray jsonEntries;
    for (TransactionEntry& entry : state->entries)
    {
        entry.hadOriginal = QFileInfo::exists(entry.targetPath);
        entry.oldDigest = entry.hadOriginal
            ? digestFile(entry.targetPath).toLatin1() : QByteArray();
        if (entry.hadOriginal && entry.oldDigest.isEmpty())
        {
            errorOrWarning = QCoreApplication::translate(
                "DurableFileTransaction",
                "无法校验原文件: %1").arg(entry.targetPath);
            return false;
        }
        if (entry.checksExpectedOriginal &&
            (entry.hadOriginal != entry.expectedOriginalExists ||
             (entry.hadOriginal &&
              entry.oldDigest != entry.expectedOldDigest)))
        {
            errorOrWarning = QCoreApplication::translate(
                "DurableFileTransaction",
                "事务目标在准备期间已被其它进程修改: %1")
                .arg(entry.targetPath);
            return false;
        }
        if (entry.removesTarget && !entry.hadOriginal)
        {
            errorOrWarning = QCoreApplication::translate(
                "DurableFileTransaction",
                "待删除目标在提交前已不存在: %1")
                .arg(entry.targetPath);
            return false;
        }

        QJsonObject object;
        object.insert("target", entry.targetRelativePath);
        object.insert("staged", entry.removesTarget ? QString() : entry.stagedRelativePath);
        object.insert("backup", entry.backupRelativePath);
        object.insert("operation", entry.removesTarget ? "remove" : "write");
        object.insert("hadOriginal", entry.hadOriginal);
        object.insert("newSha256", QString::fromLatin1(entry.newDigest));
        object.insert("oldSha256", QString::fromLatin1(entry.oldDigest));
        jsonEntries.append(object);
    }

    QJsonObject manifest;
    manifest.insert("version", ManifestVersion);
    manifest.insert("generation", state->transactionId);
    manifest.insert("entries", jsonEntries);
    const QString manifestPath = QDir(state->transactionPath).filePath(ManifestName);
    if (!writeAtomically(manifestPath, QJsonDocument(manifest).toJson(QJsonDocument::Compact)))
    {
        errorOrWarning = QCoreApplication::translate(
            "DurableFileTransaction",
            "无法写入事务清单: %1").arg(manifestPath);
        return false;
    }

    auto failAndRecover = [this, &errorOrWarning](const QString& operationError)
    {
        if (state->preserveForRecovery)
            return false;
        QStringList recoveryErrors;
        recoverTransaction(state->rootPath, state->transactionPath, recoveryErrors);
        errorOrWarning = operationError;
        if (!recoveryErrors.isEmpty())
            errorOrWarning += QCoreApplication::translate(
                "DurableFileTransaction",
                "\n恢复错误:\n%1").arg(recoveryErrors.join('\n'));
        state->finished = !QFileInfo::exists(state->transactionPath);
        if (!state->finished)
        {
            // Recovery already failed while this process still owned the
            // transaction lock. Leave the journal for the next locked startup
            // recovery instead of letting the destructor retry without a lock.
            state->preserveForRecovery = true;
        }
        removeEmptyTransactionStore(state->rootPath);
        state->lock.reset();
        state->mutationLease.release();
        return false;
    };

    if (!state->invokeFault(FaultPoint::AfterPrepared, -1, errorOrWarning))
        return failAndRecover(errorOrWarning);

    for (int index = 0; index < state->entries.size(); ++index)
    {
        const TransactionEntry& entry = state->entries[index];
        if (!entry.hadOriginal)
            continue;
        const QString backupPath = QDir(state->transactionPath)
            .filePath(entry.backupRelativePath);
        if (!QFile::rename(entry.targetPath, backupPath))
        {
            return failAndRecover(
                QCoreApplication::translate(
                    "DurableFileTransaction",
                    "无法备份原文件: %1").arg(entry.targetPath));
        }
        if (!state->invokeFault(FaultPoint::AfterBackup, index, errorOrWarning))
            return failAndRecover(errorOrWarning);
    }

    for (int index = 0; index < state->entries.size(); ++index)
    {
        const TransactionEntry& entry = state->entries[index];
        if (!entry.removesTarget)
        {
            const QString stagedPath = QDir(state->transactionPath)
                .filePath(entry.stagedRelativePath);
            if (!QDir().mkpath(QFileInfo(entry.targetPath).absolutePath()) ||
                !QFile::rename(stagedPath, entry.targetPath))
            {
                return failAndRecover(
                    QCoreApplication::translate(
                        "DurableFileTransaction",
                        "无法发布新代文件: %1").arg(entry.targetPath));
            }
        }
        if (!state->invokeFault(FaultPoint::AfterPublish, index, errorOrWarning))
            return failAndRecover(errorOrWarning);
    }

    const QString markerPath = QDir(state->transactionPath).filePath(CommitMarkerName);
    if (!writeAtomically(markerPath, state->transactionId.toUtf8()))
    {
        return failAndRecover(
            QCoreApplication::translate(
                "DurableFileTransaction",
                "无法写入事务提交标记: %1").arg(markerPath));
    }
    if (!state->invokeFault(FaultPoint::AfterCommitMarker, -1, errorOrWarning))
        return failAndRecover(errorOrWarning);

    QStringList cleanupErrors;
    if (!cleanupCommittedTransaction(
            state->transactionPath, state->entries, cleanupErrors))
    {
        errorOrWarning = QCoreApplication::translate(
            "DurableFileTransaction",
            "新代已经提交，但事务备份目录无法清理:\n%1")
            .arg(cleanupErrors.join('\n'));
    }
    state->finished = true;
    removeEmptyTransactionStore(state->rootPath);
    state->lock.reset();
    state->mutationLease.release();
    return true;
}

void DurableFileTransaction::cancel()
{
    if (!state || state->finished || state->preserveForRecovery)
        return;
    if (QFileInfo::exists(QDir(state->transactionPath).filePath(ManifestName)))
    {
        QStringList errors;
        recoverTransaction(state->rootPath, state->transactionPath, errors);
    }
    else
    {
        removeDirectory(state->transactionPath);
    }
    state->finished = !QFileInfo::exists(state->transactionPath);
    removeEmptyTransactionStore(state->rootPath);
    state->lock.reset();
    state->mutationLease.release();
}

bool DurableFileTransaction::recoverPending(const QString& rootPath,
                                             QStringList& errors)
{
    return static_cast<bool>(
        acquireRecoveredReadLock(rootPath, errors));
}

std::shared_ptr<DurableFileRecoveredReadLock>
DurableFileTransaction::acquireRecoveredReadLock(
    const QString& rootPath, QStringList& errors)
{
    errors.clear();
    AuthoringMutationGate::Lease exclusiveMutation =
        AuthoringMutationGate::instance().
            acquireExclusiveMutationLease();
    if (!exclusiveMutation)
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "资源目录正在进行其他读写，无法恢复并锁定资源");
        return {};
    }
    const QString root = normalizedRoot(rootPath);
    if (root.isEmpty() || !QFileInfo(root).isDir())
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "事务资源根无效: %1").arg(rootPath);
        return {};
    }
    if (!exclusiveMutation.addResourcePath(root))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "在线资源正在更新，无法恢复并锁定资源目录: %1")
            .arg(root);
        return {};
    }

    const QString lockPath = transactionLockPath(root);
    if (!QDir().mkpath(QFileInfo(lockPath).absolutePath()))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "无法创建事务锁目录: %1")
            .arg(QFileInfo(lockPath).absolutePath());
        return {};
    }
    auto lock = std::make_unique<QLockFile>(lockPath);
    if (!lock->tryLock(1000))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "另一个编辑器正在保存或恢复该资源目录: %1")
            .arg(root);
        return {};
    }
    if (!addPendingTransactionTargetLocks(
            root, exclusiveMutation, errors))
    {
        return {};
    }
    if (!recoverPendingUnlocked(root, errors))
    {
        return {};
    }
    if (!exclusiveMutation.
            downgradeExclusiveMutationToBlock(
                "A recovered resource read lock is active"))
    {
        errors << QCoreApplication::translate(
            "DurableFileTransaction",
            "无法将事务恢复锁切换为资源读取锁");
        return {};
    }
    auto state =
        std::make_unique<DurableFileRecoveredReadLock::State>();
    state->authoringGateLease =
        std::move(exclusiveMutation);
    state->recoveredRootKeys.insert(
        EditorAssetPath::comparisonKey(root));
    state->rootLocks.push_back(std::move(lock));
    return std::shared_ptr<DurableFileRecoveredReadLock>(
        new DurableFileRecoveredReadLock(std::move(state)));
}

QString DurableFileTransaction::transactionStorePath(const QString& rootPath)
{
    return QDir(normalizedRoot(rootPath))
        .filePath(QStringLiteral(".jxqy_editor/file_transactions"));
}

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
void DurableFileTransaction::setFaultInjectorForTests(FaultInjector injector)
{
    const std::lock_guard<std::mutex> lock(faultInjectorMutex());
    faultInjector() = std::move(injector);
}

std::unique_ptr<QLockFile>
DurableFileTransaction::acquireLegacyRootLockForTests(
    const QString& rootPath)
{
    const QString root = normalizedRoot(rootPath);
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return {};

    const QString lockPath = transactionLockPath(root);
    if (!QDir().mkpath(QFileInfo(lockPath).absolutePath()))
        return {};
    auto lock = std::make_unique<QLockFile>(lockPath);
    if (!lock->tryLock(0))
        return {};
    return lock;
}
#endif
