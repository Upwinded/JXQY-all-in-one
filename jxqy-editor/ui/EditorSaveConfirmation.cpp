#include "EditorSaveConfirmation.h"

#include "../core/DurableFileTransaction.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>

bool EditorSaveConfirmation::confirmOverwrite(QWidget* parent, const QStringList& changedPaths)
{
    QStringList paths = changedPaths;
    paths.removeDuplicates();
    QMessageBox message(QMessageBox::Warning,
        QCoreApplication::translate("EditorSaveConfirmation", "文件已有外部改动"),
        QCoreApplication::translate("EditorSaveConfirmation",
            "以下文件已被其它程序修改、删除或新建：\n%1\n\n覆盖保存将使用当前编辑内容，替换保存位置的外部版本。取消会保留当前修改，也可以返回后另存为。")
            .arg(paths.join('\n')),
        QMessageBox::NoButton, parent);
    auto* overwrite = message.addButton(
        QCoreApplication::translate("EditorSaveConfirmation", "覆盖保存"), QMessageBox::AcceptRole);
    auto* cancel = message.addButton(
        QCoreApplication::translate("EditorSaveConfirmation", "取消"), QMessageBox::RejectRole);
    message.setDefaultButton(cancel);
    message.setEscapeButton(cancel);
    message.exec();
    return message.clickedButton() == overwrite;
}

EditorFileState::OverwriteConfirmation EditorSaveConfirmation::forWindow(QWidget* parent)
{
    return [parent](const QStringList& paths) { return confirmOverwrite(parent, paths); };
}

bool EditorSaveConfirmation::prepareBytesWrite(QWidget* parent, DurableFileTransaction& transaction,
    const QString& path, const QByteArray& bytes,
    bool expectedExists, const QByteArray& expectedBytes, QString& error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        error = QCoreApplication::translate("EditorSaveConfirmation", "无法创建保存目录：%1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    const bool exists = QFileInfo::exists(path);
    QByteArray previous;
    if (exists)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            error = QCoreApplication::translate("EditorSaveConfirmation", "无法读取保存目标，当前修改已保留：%1").arg(path);
            return false;
        }
        previous = file.readAll();
        if (file.error() != QFileDevice::NoError)
        {
            error = file.errorString();
            return false;
        }
    }
    if ((exists != expectedExists || (exists && previous != expectedBytes)) && !confirmOverwrite(parent, {path}))
    {
        error.clear();
        return false;
    }
    return transaction.addBytesWriteChecked(path, bytes, exists, previous, error);
}
