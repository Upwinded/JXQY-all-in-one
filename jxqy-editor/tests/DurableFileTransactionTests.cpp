#include "../core/AuthoringMutationGate.h"
#include "../core/DurableFileTransaction.h"
#include "../core/Util.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QLockFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <iostream>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool check(bool condition, const QString& message)
{
    if (!condition)
        std::cerr << "FAILED: " << message.toUtf8().constData() << '\n';
    return condition;
}

bool writeRawFile(const QString& filePath, const QByteArray& content)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    const bool ok = file.write(content) == content.size();
    file.close();
    return ok;
}

QByteArray readRawFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool testDurableFileTransactionRecoversEveryGenerationBoundary()
{
    struct FaultReset
    {
        ~FaultReset()
        {
            DurableFileTransaction::setFaultInjectorForTests({});
        }
    } faultReset;

    struct TargetGroup
    {
        QString label;
        QStringList relativePaths;
    };
    const QList<TargetGroup> groups = {
        {QStringLiteral("map"),
         {QStringLiteral("map/scene.map"),
          QStringLiteral("ini/save/scene.npc"),
          QStringLiteral("ini/save/scene.obj"),
          QStringLiteral("mpc/map/scene/ground.mpc")}},
        {QStringLiteral("npc-object"),
         {QStringLiteral("ini/save/actor.npc"),
          QStringLiteral("ini/save/actor.obj")}},
        {QStringLiteral("menu"),
         {QStringLiteral("ini/ui/main.menu.ini"),
          QStringLiteral("ini/ui/window.ini"),
          QStringLiteral("ini/ui/start-button.ini"),
          QStringLiteral("ini/ui/system.menu.ini")}}
    };

    bool ok = true;
    auto runCrashCase = [&](const TargetGroup& group,
                            DurableFileTransaction::FaultPoint faultPoint,
                            int faultIndex,
                            bool expectNewGeneration)
    {
        QTemporaryDir directory;
        if (!check(directory.isValid(),
                   group.label + QStringLiteral(": create transaction root")))
        {
            return false;
        }

        QStringList targets;
        for (int index = 0; index < group.relativePaths.size(); ++index)
        {
            const QString target = QDir(directory.path()).filePath(
                group.relativePaths[index]);
            if (!QDir().mkpath(QFileInfo(target).absolutePath()) ||
                !writeRawFile(target,
                    QStringLiteral("old-%1-%2").arg(group.label).arg(index).toUtf8()))
            {
                return check(false,
                    group.label + QStringLiteral(": write old generation"));
            }
            targets.append(target);
        }

        DurableFileTransaction transaction(directory.path());
        QString error;
        for (int index = 0; index < targets.size(); ++index)
        {
            if (!transaction.addBytesWrite(
                    targets[index],
                    QStringLiteral("new-%1-%2").arg(group.label).arg(index).toUtf8(),
                    error))
            {
                return check(false,
                    group.label + QStringLiteral(": stage new generation: ") + error);
            }
        }

        bool injected = false;
        DurableFileTransaction::setFaultInjectorForTests(
            [&](DurableFileTransaction::FaultPoint point, int index)
            {
                if (point == faultPoint && index == faultIndex)
                {
                    injected = true;
                    return DurableFileTransaction::FaultAction::SimulateCrash;
                }
                return DurableFileTransaction::FaultAction::Continue;
            });
        const bool committed = transaction.commit(error);
        DurableFileTransaction::setFaultInjectorForTests({});
        if (!check(!committed && injected,
                   group.label + QStringLiteral(": inject requested crash boundary")))
        {
            return false;
        }

        QStringList recoveryErrors;
        if (!check(DurableFileTransaction::recoverPending(
                       directory.path(), recoveryErrors),
                   group.label + QStringLiteral(": recover transaction: ") +
                       recoveryErrors.join(QStringLiteral(" | "))))
        {
            return false;
        }

        for (int index = 0; index < targets.size(); ++index)
        {
            const QByteArray expected = QStringLiteral("%1-%2-%3")
                .arg(expectNewGeneration ? QStringLiteral("new") : QStringLiteral("old"),
                     group.label)
                .arg(index)
                .toUtf8();
            if (!check(readRawFile(targets[index]) == expected,
                       group.label + QStringLiteral(": recover one complete generation")))
            {
                return false;
            }
        }
        return check(!QFileInfo::exists(
                         DurableFileTransaction::transactionStorePath(directory.path())),
                     group.label + QStringLiteral(": clean recovered transaction store"));
    };

    for (const TargetGroup& group : groups)
    {
        ok = runCrashCase(group,
                 DurableFileTransaction::FaultPoint::AfterPrepared, -1, false) && ok;
        for (int index = 0; index < group.relativePaths.size(); ++index)
        {
            ok = runCrashCase(group,
                     DurableFileTransaction::FaultPoint::AfterBackup, index, false) && ok;
            ok = runCrashCase(group,
                     DurableFileTransaction::FaultPoint::AfterPublish, index, false) && ok;
        }
        ok = runCrashCase(group,
                 DurableFileTransaction::FaultPoint::AfterCommitMarker, -1, true) && ok;
    }

    // Cover the two asymmetric operations that are not represented by a full
    // overwrite group: a target absent in the old generation and a target
    // removed from the new generation.
    {
        QTemporaryDir directory;
        const QString replaced = QDir(directory.path()).filePath("replace.ini");
        const QString removed = QDir(directory.path()).filePath("remove.ini");
        const QString created = QDir(directory.path()).filePath("created.ini");
        ok = check(directory.isValid() &&
                       writeRawFile(replaced, "old-replaced") &&
                       writeRawFile(removed, "old-removed"),
                   "mixed transaction: write old generation") && ok;

        DurableFileTransaction transaction(directory.path());
        QString error;
        ok = check(transaction.addBytesWrite(replaced, "new-replaced", error) &&
                       transaction.addRemoval(removed, error) &&
                       transaction.addBytesWrite(created, "new-created", error),
                   "mixed transaction: stage replace, remove, and create") && ok;
        DurableFileTransaction::setFaultInjectorForTests(
            [](DurableFileTransaction::FaultPoint point, int index)
            {
                return point == DurableFileTransaction::FaultPoint::AfterPublish && index == 2
                    ? DurableFileTransaction::FaultAction::SimulateCrash
                    : DurableFileTransaction::FaultAction::Continue;
            });
        ok = check(!transaction.commit(error),
                   "mixed transaction: simulate crash after create publication") && ok;
        DurableFileTransaction::setFaultInjectorForTests({});
        QStringList recoveryErrors;
        ok = check(DurableFileTransaction::recoverPending(
                       directory.path(), recoveryErrors),
                   "mixed transaction: recover old generation") && ok;
        ok = check(readRawFile(replaced) == "old-replaced" &&
                       readRawFile(removed) == "old-removed" &&
                       !QFileInfo::exists(created),
                   "mixed transaction: restore replacement, removal, and absent target") && ok;
    }

    {
        QTemporaryDir directory;
        QTemporaryDir unrelatedDirectory;
        const QString firstTarget = QDir(directory.path()).filePath("first.ini");
        const QString secondTarget = QDir(directory.path()).filePath("second.ini");
        QString error;
        DurableFileTransaction firstTransaction(directory.path());
        ok = check(directory.isValid() && unrelatedDirectory.isValid() &&
                       firstTransaction.addBytesWrite(firstTarget, "first", error),
                   "transaction lock: first writer acquires resource-root lock") && ok;

        const QString unrelatedTarget =
            QDir(unrelatedDirectory.path()).filePath("unrelated.ini");
        DurableFileTransaction unrelatedTransaction(
            unrelatedDirectory.path());
        QString unrelatedError;
        ok = check(
                 unrelatedTransaction.addBytesWrite(
                     unrelatedTarget, "unrelated", unrelatedError) &&
                     unrelatedTransaction.commit(unrelatedError) &&
                     readRawFile(unrelatedTarget) == "unrelated",
                 "transaction lock: an unrelated resource root can save concurrently") && ok;

        DurableFileTransaction competingTransaction(directory.path());
        QString competingError;
        ok = check(!competingTransaction.addBytesWrite(
                       secondTarget, "second", competingError) &&
                       !competingError.isEmpty(),
                   "transaction lock: competing writer cannot inspect active staging") && ok;
        QStringList recoveryErrors;
        ok = check(!DurableFileTransaction::recoverPending(
                       directory.path(), recoveryErrors) &&
                       !recoveryErrors.isEmpty(),
                   "transaction lock: startup recovery cannot delete active staging") && ok;

        firstTransaction.cancel();
        DurableFileTransaction resumedTransaction(directory.path());
        error.clear();
        ok = check(resumedTransaction.addBytesWrite(
                       secondTarget, "second", error) &&
                       resumedTransaction.commit(error) &&
                       readRawFile(secondTarget) == "second" &&
                       !QFileInfo::exists(
                           DurableFileTransaction::transactionStorePath(
                               directory.path())),
                   "transaction lock: writer proceeds after prior transaction releases lock") && ok;
    }
    {
        QTemporaryDir directory;
        QTemporaryDir secondDirectory;
        ok = check(directory.isValid(),
                   "read guard compatibility: create transaction root") && ok;
        ok = check(secondDirectory.isValid(),
                   "read guard compatibility: create second transaction root") && ok;

        std::unique_ptr<QLockFile> legacyWriter =
            DurableFileTransaction::acquireLegacyRootLockForTests(
                directory.path());
        QStringList recoveryErrors;
        auto blockedReadGuard =
            DurableFileTransaction::acquireRecoveredReadLock(
                directory.path(), recoveryErrors);
        ok = check(
                 legacyWriter &&
                     !blockedReadGuard &&
                     !recoveryErrors.isEmpty(),
                 "read guard compatibility: legacy per-root writer blocks"
                 " current recovery reader") && ok;

        legacyWriter.reset();
        recoveryErrors.clear();
        auto readGuard =
            DurableFileTransaction::acquireRecoveredReadLock(
                directory.path(), recoveryErrors);
        const bool secondRootAdded = readGuard &&
            readGuard->addRecoveredReadRoot(
                secondDirectory.path(), recoveryErrors);
        std::unique_ptr<QLockFile> blockedLegacyWriter =
            DurableFileTransaction::acquireLegacyRootLockForTests(
                directory.path());
        std::unique_ptr<QLockFile> blockedSecondLegacyWriter =
            DurableFileTransaction::acquireLegacyRootLockForTests(
                secondDirectory.path());
        DurableFileTransaction blockedCurrentWriter(directory.path());
        QString blockedCurrentWriterError;
        const bool blockedCurrentWriterPrepared =
            blockedCurrentWriter.addBytesWrite(
                QDir(directory.path()).filePath("blocked.ini"),
                QByteArray("blocked"),
                blockedCurrentWriterError);
        ok = check(
                 readGuard &&
                     secondRootAdded &&
                     recoveryErrors.isEmpty() &&
                     !blockedLegacyWriter &&
                     !blockedSecondLegacyWriter &&
                     !blockedCurrentWriterPrepared &&
                     !blockedCurrentWriterError.isEmpty(),
                 "read guard compatibility: returned guard retains both"
                 " current per-root locks") && ok;

        readGuard.reset();
        std::unique_ptr<QLockFile> resumedLegacyWriter =
            DurableFileTransaction::acquireLegacyRootLockForTests(
                directory.path());
        std::unique_ptr<QLockFile> resumedSecondLegacyWriter =
            DurableFileTransaction::acquireLegacyRootLockForTests(
                secondDirectory.path());
        ok = check(
                 resumedLegacyWriter &&
                     resumedSecondLegacyWriter,
                 "read guard compatibility: releasing guard releases both"
                 " current and legacy locks") && ok;
        resumedLegacyWriter.reset();
        resumedSecondLegacyWriter.reset();

        DurableFileTransaction resumedCurrentWriter(directory.path());
        QString resumedCurrentWriterError;
        const QString resumedTarget =
            QDir(directory.path()).filePath("resumed.ini");
        ok = check(
                 resumedCurrentWriter.addBytesWrite(
                     resumedTarget,
                     QByteArray("resumed"),
                     resumedCurrentWriterError) &&
                     resumedCurrentWriter.commit(
                         resumedCurrentWriterError) &&
                     readRawFile(resumedTarget) == QByteArray("resumed"),
                 "read guard compatibility: current writer resumes after"
                 " both read locks are released") && ok;
    }
    {
        QTemporaryDir directory;
        const QString target = QDir(directory.path()).filePath("safe.ini");
        const QString transactionPath = QDir(
            DurableFileTransaction::transactionStorePath(directory.path()))
                .filePath("tampered");
        const QString manifestPath = QDir(transactionPath).filePath("manifest.json");
        ok = check(directory.isValid() &&
                       QDir().mkpath(transactionPath) &&
                       writeRawFile(target, "safe") &&
                       writeRawFile(manifestPath,
                           "{\"version\":1,\"generation\":\"tampered\",\"entries\":[{"
                           "\"target\":\"safe.ini\",\"staged\":\"staged/0000.new\","
                           "\"backup\":\"manifest.json\",\"operation\":\"write\","
                           "\"hadOriginal\":true,"
                           "\"newSha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
                           "\"oldSha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}]}"),
                   "tampered transaction: create malicious backup path fixture") && ok;
        QStringList recoveryErrors;
        ok = check(!DurableFileTransaction::recoverPending(
                       directory.path(), recoveryErrors) &&
                       readRawFile(target) == "safe" &&
                       QFileInfo::exists(manifestPath),
                   "tampered transaction: reject manifest without touching target data") && ok;
    }
    return ok;
}

bool testAuthoringMutationGateProtectsLowLevelWritesAndRecovery()
{
    struct FaultReset
    {
        ~FaultReset()
        {
            DurableFileTransaction::setFaultInjectorForTests({});
        }
    } faultReset;

    QTemporaryDir mutationDirectory;
    QTemporaryDir recoveryDirectory;
    if (!check(mutationDirectory.isValid() &&
                   recoveryDirectory.isValid(),
               "authoring mutation gate: create isolated resource roots"))
    {
        return false;
    }

    const QString writeTarget =
        QDir(mutationDirectory.path()).filePath("write.bin");
    const QString renameSource =
        QDir(mutationDirectory.path()).filePath("rename-source.bin");
    const QString renameDestination =
        QDir(mutationDirectory.path()).filePath("rename-destination.bin");
    const QString removeTarget =
        QDir(mutationDirectory.path()).filePath("remove.bin");
    const QString transactionTarget =
        QDir(mutationDirectory.path()).filePath("transaction.bin");
    const QByteArray originalWriteBytes("original-write");
    const QByteArray originalRenameBytes("original-rename");
    const QByteArray originalRemoveBytes("original-remove");
    const QByteArray originalTransactionBytes("original-transaction");
    if (!check(writeRawFile(writeTarget, originalWriteBytes) &&
                   writeRawFile(renameSource, originalRenameBytes) &&
                   writeRawFile(removeTarget, originalRemoveBytes) &&
                   writeRawFile(
                       transactionTarget,
                       originalTransactionBytes),
               "authoring mutation gate: seed direct and transactional targets"))
    {
        return false;
    }

    const QString recoveryTarget =
        QDir(recoveryDirectory.path()).filePath("recovery.bin");
    const QByteArray recoveryOldBytes("recovery-old");
    const QByteArray recoveryPublishedBytes("recovery-published");
    if (!check(writeRawFile(recoveryTarget, recoveryOldBytes),
               "authoring mutation gate: seed recovery target"))
    {
        return false;
    }
    {
        DurableFileTransaction interruptedTransaction(
            recoveryDirectory.path());
        QString error;
        if (!check(
                interruptedTransaction.addBytesWrite(
                    recoveryTarget,
                    recoveryPublishedBytes,
                    error),
                QStringLiteral(
                    "authoring mutation gate: prepare interrupted transaction: %1")
                    .arg(error)))
        {
            return false;
        }
        DurableFileTransaction::setFaultInjectorForTests(
            [](DurableFileTransaction::FaultPoint point, int index)
            {
                return point ==
                           DurableFileTransaction::FaultPoint::AfterPublish &&
                        index == 0
                    ? DurableFileTransaction::FaultAction::SimulateCrash
                    : DurableFileTransaction::FaultAction::Continue;
            });
        const bool committed =
            interruptedTransaction.commit(error);
        DurableFileTransaction::setFaultInjectorForTests({});
        if (!check(
                !committed &&
                    readRawFile(recoveryTarget) ==
                        recoveryPublishedBytes &&
                    QFileInfo::exists(
                        DurableFileTransaction::transactionStorePath(
                            recoveryDirectory.path())),
                "authoring mutation gate: leave a published generation pending recovery"))
        {
            return false;
        }
    }

    bool ok = true;
    AuthoringMutationGate::Lease blockingLease =
        AuthoringMutationGate::instance().acquireLease(
            "test read-only desktop-run session");
    ok = check(
             blockingLease.active(),
             "authoring mutation gate: acquire formal-resource mutation block") &&
        ok;

    const QByteArray blockedWriteBytes("blocked-write");
    const bool directWriteSucceeded =
        Util::writeFileFromBuffer(
            writeTarget.toUtf8().toStdString(),
            blockedWriteBytes.constData(),
            static_cast<std::size_t>(blockedWriteBytes.size()));
    const bool directRenameSucceeded =
        Util::renameFileUtf8(
            renameSource.toUtf8().toStdString(),
            renameDestination.toUtf8().toStdString());
    const bool directRemoveSucceeded =
        Util::removeFileUtf8(
            removeTarget.toUtf8().toStdString());
    ok = check(
             !directWriteSucceeded &&
                 !directRenameSucceeded &&
                 !directRemoveSucceeded &&
                 readRawFile(writeTarget) ==
                     originalWriteBytes &&
                 readRawFile(renameSource) ==
                     originalRenameBytes &&
                 !QFileInfo::exists(renameDestination) &&
                 readRawFile(removeTarget) ==
                     originalRemoveBytes,
             "authoring mutation gate: direct write, rename, and remove fail before bytes mutate") &&
        ok;
    ok = check(
             blockingLease.active() &&
                 AuthoringMutationGate::instance().
                     isMutationBlocked(),
             "authoring mutation gate: direct-operation rejection retains the blocking lease") &&
        ok;

    {
        DurableFileTransaction blockedTransaction(
            mutationDirectory.path());
        QString prepareError;
        const bool prepared =
            blockedTransaction.addBytesWrite(
                transactionTarget,
                QByteArray("blocked-transaction"),
                prepareError);
        QString commitError;
        const bool committed =
            blockedTransaction.commit(commitError);
        ok = check(
                 !prepared &&
                     !prepareError.isEmpty() &&
                     !committed &&
                     !commitError.isEmpty() &&
                     readRawFile(transactionTarget) ==
                         originalTransactionBytes &&
                     !QFileInfo::exists(
                         DurableFileTransaction::transactionStorePath(
                             mutationDirectory.path())),
                 "authoring mutation gate: transaction staging and commit fail before creating a store or changing bytes") &&
            ok;
    }
    ok = check(
             blockingLease.active() &&
                 AuthoringMutationGate::instance().
                     isMutationBlocked(),
             "authoring mutation gate: transaction rejection retains the blocking lease") &&
        ok;

    QStringList blockedRecoveryErrors;
    const bool blockedRecovery =
        DurableFileTransaction::recoverPending(
            recoveryDirectory.path(),
            blockedRecoveryErrors);
    ok = check(
             !blockedRecovery,
             "authoring mutation gate: recovery returns failure while mutation is blocked") &&
        ok;
    ok = check(
             !blockedRecoveryErrors.isEmpty(),
             "authoring mutation gate: blocked recovery reports an error") &&
        ok;
    ok = check(
             readRawFile(recoveryTarget) ==
                 recoveryPublishedBytes,
             "authoring mutation gate: blocked recovery does not roll back published bytes") &&
        ok;
    ok = check(
             QFileInfo::exists(
                 DurableFileTransaction::transactionStorePath(
                     recoveryDirectory.path())),
             "authoring mutation gate: blocked recovery preserves the pending journal") &&
        ok;

    blockingLease.release();
    QStringList recoveryErrors;
    ok = check(
             DurableFileTransaction::recoverPending(
                 recoveryDirectory.path(),
                 recoveryErrors) &&
                 recoveryErrors.isEmpty() &&
                 readRawFile(recoveryTarget) ==
                     recoveryOldBytes &&
                 !QFileInfo::exists(
                     DurableFileTransaction::transactionStorePath(
                         recoveryDirectory.path())),
             "authoring mutation gate: pending recovery resumes after the blocking lease releases") &&
        ok;

    const QByteArray releasedWriteBytes("released-write");
    ok = check(
             Util::writeFileFromBuffer(
                 writeTarget.toUtf8().toStdString(),
                 releasedWriteBytes.constData(),
                 static_cast<std::size_t>(
                     releasedWriteBytes.size())) &&
                 Util::renameFileUtf8(
                     renameSource.toUtf8().toStdString(),
                     renameDestination.toUtf8().toStdString()) &&
                 Util::removeFileUtf8(
                     removeTarget.toUtf8().toStdString()) &&
                 readRawFile(writeTarget) ==
                     releasedWriteBytes &&
                 !QFileInfo::exists(renameSource) &&
                 readRawFile(renameDestination) ==
                     originalRenameBytes &&
                 !QFileInfo::exists(removeTarget),
             "authoring mutation gate: direct operations resume after the blocking lease releases") &&
        ok;

    {
        DurableFileTransaction resumedTransaction(
            mutationDirectory.path());
        QString error;
        ok = check(
                 resumedTransaction.addBytesWrite(
                     transactionTarget,
                     QByteArray("released-transaction"),
                     error) &&
                     resumedTransaction.commit(error) &&
                     readRawFile(transactionTarget) ==
                         QByteArray("released-transaction"),
                 QStringLiteral(
                     "authoring mutation gate: transaction resumes after the blocking lease releases: %1")
                     .arg(error)) &&
            ok;
    }

    return ok;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    return testDurableFileTransactionRecoversEveryGenerationBoundary() &&
            testAuthoringMutationGateProtectsLowLevelWritesAndRecovery()
        ? 0
        : 1;
}
