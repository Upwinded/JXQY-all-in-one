#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QLockFile;

class DurableFileRecoveredReadLock
{
public:
    ~DurableFileRecoveredReadLock();

    DurableFileRecoveredReadLock(
        const DurableFileRecoveredReadLock&) = delete;
    DurableFileRecoveredReadLock& operator=(
        const DurableFileRecoveredReadLock&) = delete;

    // Add another source or destination path to the current coherent read.
    bool addResourcePath(const QString& path);
    // Recover and lock one more transaction root under the same read snapshot.
    // This is required when one import reads files from multiple directories
    // that may each contain an interrupted editor transaction.
    bool addRecoveredReadRoot(
        const QString& rootPath, QStringList& errors);

private:
    friend class DurableFileTransaction;
    struct State;

    explicit DurableFileRecoveredReadLock(
        std::unique_ptr<State> state);

    std::unique_ptr<State> state;
};

class DurableFileTransaction
{
public:
    enum class FaultPoint
    {
        AfterPrepared,
        AfterBackup,
        AfterPublish,
        AfterCommitMarker
    };

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
    enum class FaultAction
    {
        Continue,
        Fail,
        SimulateCrash
    };

    using FaultInjector = std::function<FaultAction(FaultPoint, int)>;
#endif

    explicit DurableFileTransaction(const QString& rootPath);
    ~DurableFileTransaction();

    DurableFileTransaction(const DurableFileTransaction&) = delete;
    DurableFileTransaction& operator=(const DurableFileTransaction&) = delete;

    bool addPreparedWrite(const QString& targetPath,
                          const QString& preparedPath,
                          QString& errorMessage);
    bool addBytesWrite(const QString& targetPath,
                       const QByteArray& bytes,
                       QString& errorMessage);
    bool addBytesWriteChecked(
        const QString& targetPath,
        const QByteArray& bytes,
        bool expectedTargetExists,
        const QByteArray& expectedTargetBytes,
        QString& errorMessage);
    bool addRemoval(const QString& targetPath, QString& errorMessage);

    // A successful commit may still return a non-empty warning when the new
    // generation is durable but obsolete backup files could not be removed.
    bool commit(QString& errorOrWarning);
    void cancel();

    static bool recoverPending(const QString& rootPath, QStringList& errors);
    // Recover any interrupted write and retain the same inter-process lock
    // while callers read a coherent generation. Releasing the returned guard
    // allows another editor process to begin a transaction.
    static std::shared_ptr<DurableFileRecoveredReadLock>
    acquireRecoveredReadLock(
        const QString& rootPath, QStringList& errors);
    static QString transactionStorePath(const QString& rootPath);

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
    static void setFaultInjectorForTests(FaultInjector injector);
    static std::unique_ptr<QLockFile> acquireLegacyRootLockForTests(
        const QString& rootPath);
#endif

private:
    struct State;
    std::unique_ptr<State> state;
};
