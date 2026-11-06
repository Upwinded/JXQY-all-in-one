#include "EditorFileState.h"

#include "DurableFileTransaction.h"
#include "EditorAssetPath.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

EditorFileState::Snapshot EditorFileState::readSnapshot(const QString& path)
{
    Snapshot result;
    if (path.isEmpty())
        return result;
    result.path = EditorAssetPath::normalizedAbsolutePath(path);
    const QFileInfo info(result.path);
    result.exists = info.exists();
    if (!result.exists)
    {
        result.readable = true;
        return result;
    }
    QFile file(result.path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (info.isFile() && file.open(QIODevice::ReadOnly) && hash.addData(&file) && file.error() == QFileDevice::NoError)
    {
        result.sha256 = hash.result().toHex();
        result.readable = true;
    }
    return result;
}

EditorFileState EditorFileState::read(const QString& sourcePath, const QString& targetPath)
{
    EditorFileState result;
    result.source = readSnapshot(sourcePath);
    result.target = EditorAssetPath::logicalComparisonKey(sourcePath) == EditorAssetPath::logicalComparisonKey(targetPath)
        ? result.source : readSnapshot(targetPath);
    return result;
}

bool EditorFileState::isReadable() const
{
    return source.exists && source.readable && target.readable;
}

EditorFileState EditorFileState::newFile(const QString& targetPath)
{
    EditorFileState result;
    result.source.path = EditorAssetPath::normalizedAbsolutePath(targetPath);
    result.source.readable = true;
    result.target = result.source;
    return result;
}

EditorFileState EditorFileState::forSave(const QString& targetPath) const
{
    return !target.path.isEmpty() && EditorAssetPath::logicalComparisonKey(targetPath) == EditorAssetPath::logicalComparisonKey(target.path)
        ? *this : read(targetPath, targetPath);
}

bool EditorFileState::prepareWrite(DurableFileTransaction& transaction,
    const QString& targetPath, const QString& temporaryPath,
    EditorFileState& savedState, QString& errorMessage,
    const OverwriteConfirmation& confirmOverwrite) const
{
    const bool ordinarySave = !target.path.isEmpty() &&
        EditorAssetPath::logicalComparisonKey(targetPath) == EditorAssetPath::logicalComparisonKey(target.path);
    Snapshot expected = ordinarySave ? target : readSnapshot(targetPath);
    Snapshot currentSource;
    if (ordinarySave)
        currentSource = readSnapshot(source.path);
    const Snapshot currentTarget = ordinarySave &&
        EditorAssetPath::logicalComparisonKey(source.path) == EditorAssetPath::logicalComparisonKey(targetPath)
        ? currentSource : readSnapshot(targetPath);
    QStringList changedPaths;
    if (ordinarySave && (!source.readable || !currentSource.readable ||
        currentSource.exists != source.exists || currentSource.sha256 != source.sha256))
        changedPaths.append(source.path);
    if (ordinarySave && (!currentTarget.readable || currentTarget.exists != target.exists || currentTarget.sha256 != target.sha256))
        changedPaths.append(targetPath);
    changedPaths.removeDuplicates();
    if (!changedPaths.isEmpty())
    {
        errorMessage = QCoreApplication::translate("EditorFileState",
            "文件已被其它程序修改、删除或新建，未覆盖：\n%1\n当前修改仍保留，可以另存为。")
            .arg(changedPaths.join('\n'));
        if (!currentTarget.readable || !confirmOverwrite)
            return false;
        if (!confirmOverwrite(changedPaths))
        {
            errorMessage.clear();
            return false;
        }
        expected = currentTarget;
    }
    if (!expected.readable)
    {
        errorMessage = QCoreApplication::translate("EditorFileState", "无法读取保存目标，未覆盖：%1").arg(targetPath);
        return false;
    }
    const Snapshot staged = readSnapshot(temporaryPath);
    if (!staged.exists || !staged.readable)
    {
        errorMessage = QCoreApplication::translate("EditorFileState", "无法读取待保存内容：%1").arg(temporaryPath);
        return false;
    }
    if (!transaction.addPreparedWriteChecked(targetPath, temporaryPath,
            expected.exists, expected.sha256, errorMessage))
        return false;
    savedState.source = staged;
    savedState.source.path = EditorAssetPath::normalizedAbsolutePath(targetPath);
    savedState.target = savedState.source;
    return true;
}
